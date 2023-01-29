#include "cpubus.h"

#include <string.h>

#include "main.h"
#include "opcodes.h"
#include "debug.h"
#include "gamedb/game_detection.h"

#include "pico/stdlib.h"
#include "pico/mutex.h"

#include "hardware/pio.h"
#include "memory-bus.pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/structs/systick.h"

uint32_t cycleRatio; //Ratio of rp2040 cycles to Game Boy cycles.
#define CYCLE_RATIO_STATISTIC_SKIP 250 //How many cycles to skip before building the statistic
#define CYCLE_RATIO_STATISTIC_SIZE 1000 //How many cycles to capture as a statistic for cycleRatio

PIO busPIO;
uint32_t busPIOemptyMask, busPIOstallMask;
uint busSM;
io_ro_32 volatile * rxf;

uint32_t volatile rawBusData;
uint8_t volatile * opcode = (uint8_t*)(&rawBusData) + 2; // The rp2040 is little endian!
uint16_t volatile * address = (uint16_t*)(&rawBusData);
uint8_t volatile * extra = (uint8_t*)(&rawBusData) + 3;

//Used to remember previous memory data to dump a history if needed
uint32_t history[256]; //Buffer for memory events
uint volatile cycleIndex; // Just counting cycles. Lowest byte can be used as index to the cyclic history array and the second byte is used as the Game Boy's DIV register
uint8_t volatile * historyIndex = (uint8_t *)&cycleIndex; //Index for history array, lowest byte of cycleIndex
uint volatile div; //cycle that corresponds to DIV register equaling zero
uint8_t readAheadIndex;

mutex_t cpubusMutex;

bool volatile running = false;
const volatile char * error;
bool volatile errorIsStall;
int volatile errorOpcode;
uint delayedOpcodeCount = 0; //Counts the number of times we did not see a new clock from the Game Boy when expected in order to detect a halt state

uint ignoreCycles; //(Remaining) number of cycles to ignore, typically during DMA. Will try to detect a ret instruction to find back.

uint8_t volatile memory[0x010000]; //We are actually only interested in 0x8000 to 0xffff in the Game Boy's address space. We don't care about the cartridge, because we get fresh data from the real one whenever the Game Boy reads it. However, wasting 32kB here by reserving this for the rare cases of writing to a cartridge, allows us to do all other memory writes without a range check.

//DMA from memory
int oamDmaChannel;
dma_channel_config oamDmaConfig;

//DMA from cartridge
bool cartridgeDMA = false;
uint cartridgeDMAsrc;
uint cartridgeDMAdst;


//CPU registers
uint8_t registers[8];        // The rp2040 is little endian! So we need to swap registers in memory if we want to access them with a pointer like *bc and *b
uint8_t * c = registers;     //With swapped registers, opcodes can be filtered to address them via (opcode & 0x07) ^ 0x01
uint8_t * b = registers + 1;
uint8_t * e = registers + 2;
uint8_t * d = registers + 3;
uint8_t * l = registers + 4;
uint8_t * h = registers + 5;
uint8_t * a = registers + 6;
//Index 7 is unused, but we reserve it to allow for faster 2x 32bit copies for example if register debugging is enabled.
uint16_t * bc = (uint16_t *)registers;
uint16_t * de = (uint16_t *)(registers + 2);
uint16_t * hl = (uint16_t *)(registers + 4);
uint16_t sp = 0;

uint32_t flags; // Plenty of memory, so instead of using a bit mask, we four bytes pointing at a uint32_t. As we are on a 32bit architecture, this allows us to quickly reset all flags while still using them individually like bools
uint8_t * Z = (uint8_t *)(&flags);
uint8_t * N = (uint8_t *)(&flags)+1;
uint8_t * H = (uint8_t *)(&flags)+2;
uint8_t * C = (uint8_t *)(&flags)+3;

bool interruptsEnabled;
uint interruptsEnableCycle; //Keeps track of when interrupts were enabled. If the interrupt occurs immediately after enabling, it has probably been delayed and we should not use it to sync the PPU


void setupPIO() {
    for (int i = 2; i < 30; i++)
        gpio_init(i);
    busPIO = pio0;
    busSM = pio_claim_unused_sm(busPIO, true);
    uint offset = pio_add_program(busPIO, &memoryBus_program);
    memoryBus_program_init(busPIO, busSM, offset, (float)clock_get_hz(clk_sys) / 10e6);
    pio_sm_set_enabled(busPIO, busSM, true);
    busPIOemptyMask = 1u << (PIO_FSTAT_RXEMPTY_LSB + busSM);
    busPIOstallMask = 1u << (PIO_FDEBUG_RXSTALL_LSB + busSM);
    rxf = busPIO->rxf + busSM;
}

void stop(const char* errorMsg) {
    if (running) { //Avoid overwriting a previous reason to stop
        running = false;
        error = errorMsg;
    }
}

void setupOamDMA() {
    oamDmaChannel = dma_claim_unused_channel(true);
    oamDmaConfig = dma_channel_get_default_config(oamDmaChannel);
    channel_config_set_read_increment(&oamDmaConfig, true);
    channel_config_set_write_increment(&oamDmaConfig, true);
}

void dmaToOAM(uint16_t source) {
    if (dma_channel_is_busy(oamDmaChannel)) {
        stop("DMA started while channel busy.");
        return;
    }
    dma_channel_configure(oamDmaChannel, &oamDmaConfig, &memory[0xfe00], &memory[source], 0xa0 / 4, true);
}

void reset() {
    cycleIndex = 0;
    readAheadIndex = HISTORY_READAHEAD;
    div = cycleIndex - 0x0000ab00u; //Starts at 0xab

    ignoreCycles = 0;

    error = NULL;
    errorOpcode = -1;
    errorIsStall = false;

    *a = 0x01;
    *b = 0x00;
    *c = 0x13;
    *d = 0x00;
    *e = 0xd8;
    *h = 0x01;
    *l = 0x4d;
    sp = 0xfffe;
    flags = 0x01010001;

    interruptsEnabled = false;
    interruptsEnableCycle = 0;

    cartridgeDMA = false;

    memset((void*)memory, 0, sizeof(memory));

    toMemory(0xff04, 0xab);
    toMemory(0xff05, 0x00);
    toMemory(0xff06, 0x00);
    toMemory(0xff07, 0x00);
    toMemory(0xff10, 0x80);
    toMemory(0xff11, 0xbf);
    toMemory(0xff12, 0xf3);
    toMemory(0xff14, 0xbf);
    toMemory(0xff16, 0x3f);
    toMemory(0xff17, 0x00);
    toMemory(0xff19, 0xbf);
    toMemory(0xff1a, 0x7f);
    toMemory(0xff1b, 0xff);
    toMemory(0xff1c, 0x9f);
    toMemory(0xff1e, 0xbf);
    toMemory(0xff20, 0xff);
    toMemory(0xff21, 0x00);
    toMemory(0xff22, 0x00);
    toMemory(0xff23, 0xbf);
    toMemory(0xff24, 0x77);
    toMemory(0xff25, 0xf3);
    toMemory(0xff26, 0xf1);
    toMemory(0xff40, 0x91);
    toMemory(0xff42, 0x00);
    toMemory(0xff43, 0x00);
    toMemory(0xff45, 0x00);
    toMemory(0xff47, 0xfc);
    toMemory(0xff48, 0xff);
    toMemory(0xff49, 0xff);
    toMemory(0xff4a, 0x00);
    toMemory(0xff4b, 0x00);
    toMemory(0xffff, 0x00);

    resetHashes();
}

void inline substitudeBusdataFromMemory() {
    if ((*address & 0x8000) != 0 && ((*address & 0xe000) != 0xa000)) { //Neither ROM 0x0000-0x7fff nor external RAM 0xa000-0xbfff
        //This is from RAM, load our version as we cannot see the data on the bus
        *opcode = memory[*address];
        history[*historyIndex] = rawBusData;
    }
}

void getNextFromBus() {

    while ((busPIO->fstat & busPIOemptyMask) != 0) { //Wait if we are here to soon
        if (systick_hw->csr & 0x00010000) { //Triggered at the rate of the Game Boy clock
            if (running) { //No substitude clock if we are just waiting for the game to be turned on.
                delayedOpcodeCount++;
                if (delayedOpcodeCount > 3) { //First read of csr will always have the COUNTFLAG set, next flag might occur under a Game Boy cycle, but the one after that truely means that the clock is missing
                    //Clock is gone, let's generate our own events.
                    if ((uint8_t)(history[readAheadIndex] >> 16) != 0x76 && (uint8_t)(history[(uint8_t)(readAheadIndex-1)] >> 16) == 0x76) {
                        // We get one wrong dataset when the clock is turned off. Let's just replace it with halt, which is effectively a NOOP.
                        history[readAheadIndex] = history[(uint8_t)(readAheadIndex-1)];
                    }
                    cycleIndex++;
                    readAheadIndex++;
                    history[readAheadIndex] = history[(uint8_t)(readAheadIndex-1)];
                    rawBusData = history[*historyIndex];
                    substitudeBusdataFromMemory();
                    if (delayedOpcodeCount > CYCLES_PER_FRAME) {
                        //This should not happen unless the Game Boy has been turned off.
                        //I can imaginge that a game could wait indefinitely for a gamepad input (can it?), but not using vblank to have anything active on the screen would be unusual.
                        //If we find a game that waits longer than one frame, we need to check which interrupts are enabled and will not have a chance to determine if the Game Boy was turned off if only the gamepad interrupt is enabled.
                        stop("Halt timed out.");
                    }
                    return;
                }
            } else
                return;
        }
    }

    delayedOpcodeCount = 0;
    cycleIndex++;
    readAheadIndex++;
    history[readAheadIndex] = *rxf;
    rawBusData = history[*historyIndex];
    substitudeBusdataFromMemory();
}

void handleMemoryBus() { //To be executed on second core
    setupPIO();
    setupOamDMA();
    mutex_init(&cpubusMutex);
    mutex_enter_blocking(&cpubusMutex); //Default is that this thread is in charge of the bus and its history array. We only yield occasionally.

    while (1) {
        reset();

        //Wait for game to actually start and use this to determine the cycleRatio
        uint leadIn = CYCLE_RATIO_STATISTIC_SKIP; //Skip first cycles in case something funny triggered a few extras while turning on.
        uint count = CYCLE_RATIO_STATISTIC_SIZE;
        systick_hw->rvr = 0x00FFFFFF;
        systick_hw->csr = 0x4;
        do {
            getNextFromBus();
            if (leadIn) {
                leadIn--;
                if (!leadIn) {
                    systick_hw->csr = 0x5;
                }
            } else if (count) {
                count--;
                if (!count) {
                    cycleRatio = (0x00FFFFFF - systick_hw->cvr) / CYCLE_RATIO_STATISTIC_SIZE;
                    systick_hw->rvr = cycleRatio-1;
                }
            }
        } while (*address != 0x0100);

        running = true;

        busPIO->fdebug = busPIOstallMask; //Clear stall flag

        while (running) {

            //Ignore events during DMA
            while (ignoreCycles) {
                getNextFromBus();
                if (cartridgeDMA && *address == cartridgeDMAsrc) {
                    memory[cartridgeDMAdst] = *opcode;
                    cartridgeDMAsrc++;
                    cartridgeDMAdst++;
                    if (cartridgeDMAdst >= 0xfea0)
                        cartridgeDMA = false;
                }
                ignoreCycles--;
                if (ignoreCycles == 10) { //Some games copy some HRAM/IO addresses during DMA (Tetris 2). We do this a few cycles before DMA ends.
                    for (uint i = 0; i < DMA_REGISTER_MAP_SIZE; i += 2) {
                        if (gameInfo.writeRegistersDuringDMA[i] == 0x00)
                            break;
                        toMemory(0xff00 | gameInfo.writeRegistersDuringDMA[i+1], memory[0xff00 | gameInfo.writeRegistersDuringDMA[i]]); //Note: Using fromMemory does not make sense here because it would try to use the opcode data filled in by getNextFromBus, which is not relevant as we are not seeing correct addresses on the bus.
                    }
                } else if (ignoreCycles == 0) { //We are done, but we now have to look for a return instruction to sync back up with the CPU which was doing unknown instructions during DMA
                    bool synchronized = false;
                    int wait = 0;
                    while (!synchronized) {
                        if (*address == sp) {
                            synchronized = true;
                            getNextFromBus();
                            getNextFromBus();
                            getNextFromBus();
                            sp += 2;
                            break;
                        } else if (gameInfo.dmaFix != 0x0000 && *address == gameInfo.dmaFix) {
                            synchronized = true;
                            break;
                        }
                        getNextFromBus();
                        wait++;
                        if (wait > 20) {
                            stop("Could not find a ret after DMA.");
                            break;
                        }
                    }
                }
            }

            //Detect interrupts
            if   ( (history[readAheadIndex] & 0x0000ffc7) == 0x0040             //fifth instruction continues from 0x0040, 0x0048, 0x0050, 0x0058 or 0x0060 (this bitmask permits some rare and unlikely edge cases)
                && (uint16_t)history[(uint8_t)(readAheadIndex-2)] == sp-1       //third instruction has address of decremented stack pointer
                && (uint16_t)history[(uint8_t)(readAheadIndex-1)] == sp-2       //fourth instruction has decremented it even further
                && (uint16_t)history[readAheadIndex] != *address+2) {           //The PC has jumped compared to the fifth instruction (see below, this check is almost redundant after the first one and should only capture the last edge cases.) 
                // This is an interrupt. These are tricky to catch as two seemingly random reads are done first
                // which can easily be mistaken for opcodes that are actully executed. This is why we do the read
                // ahead, so we can see if the instruction after the next one reads the sp register. Additionally,
                // the one after that should read a decremented sp register, which should set this appart from
                // other sp related instructions except for a call or push.
                // Finally, we need to check that we actually jump instead of executing the next opcode, because
                // otherwise it could be mixed up with any 1 cycle opcode followed by a push. (Hence *address+2 as last check above.)
                // The good news is that we only need to do the sp operation and burn five cycles in total. It should
                // be quite rare that we need to do more than the first check and if all succeed we can catch up.
                
                #ifdef DEBUG_EVENTS
                history[*historyIndex] |= 0x02000000; //Use this bit to mark this event as an interrupt for debugging
                #endif
                uint16_t oldAddress = (uint16_t)history[(uint8_t)(*historyIndex - 1)];
                toMemory(--sp, oldAddress >> 8);
                toMemory(--sp, (uint8_t)oldAddress);
                getNextFromBus();
                getNextFromBus();
                getNextFromBus();
                getNextFromBus();
                getNextFromBus();
                if (interruptsEnabled && (cycleIndex - interruptsEnableCycle > 16 || gameInfo.useImmediateIRQ)) {
                    if (*address == 0x0040) { //vsync, set PPU to the beginning of vsync plus a few cycles that it took to get here.
                        vblankOffset = (144 - y) * CYCLES_PER_LINE - lineCycle - 6;
                        if (vblankOffset > CYCLES_PER_FRAME/2)
                            vblankOffset -= CYCLES_PER_FRAME;
                    }
                }
                interruptsEnabled = false;
            }

            //Execute an opcode
            #ifdef DEBUG_EVENTS
            history[*historyIndex] |= 0x01000000; //Use this bit to mark this event as an opcode for debugging
            #endif
            DEBUG_TRIGGER_LOG_REGISTERS
            (*opcodes[*opcode])();

            // Debugging Breakpoint at specific address
            DEBUG_TRIGGER_BREAKPOINT_AT_ADDRESS

            //Check if we missed an instruction and stop if we did.
            if (busPIO->fdebug & busPIOstallMask) {
                stop("PIO stalled.");
                errorIsStall = true;
            }
        }

        //Collect following instructions to get context for dump
        if (error != NULL) {
            for (uint8_t i = 0; i < DUMPMORE - HISTORY_READAHEAD; i++) {
                getNextFromBus();
            }
            mutex_exit(&cpubusMutex);
            sleep_ms(20); //If error is not null, we have to temporarily hand over control of the history to the main thread, so it can dump the data without mixing different outputs in stdio (if we dumped on this thread here) or messing up the dump (if we read more bus events while dumping)
            mutex_enter_blocking(&cpubusMutex);
        }
        
    }
}

