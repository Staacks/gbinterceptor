#ifndef GBINTERCEPTOR_DEBUG
#define GBINTERCEPTOR_DEBUG

#include "pico/stdlib.h"
#include "ppu.h"

#define DEBUG_EVENTS        //Mark events like opcodes or interrupts in the bus history. This is mostly harmless, but disabling it might save a few cycles in a problematic part of a game.
//#define DEBUG_GAME_DETECTION  //Periodically outputs the current hashes used for game detection
//#define DEBUG_PPU_TIMING    //Periodically measures the timing of PPU rendering and outputs it via USB serial
//#define DEBUG_MEMORY_DUMP //Disables rendering and periodically dumps the emulated memory to USB serial
//#define DEBUG_BREAKPOINT_AT_ADDRESS 0x3926 //Trigger a breakpoint if an opcode at the given address is about to be executed and dump memory and opcode history
//#define DEBUG_BREAKPOINT_AT_ADDRESS_IGNORE 73 //The break at DEBUG_BREAKPOINT_AT_ADDRESS will be ignored n times.
//#define DEBUG_BREAKPOINT_AT_WRITE_TO_ADDRESS 0xc854 //Trigger a breakpoint if data is written to a specific address
//#define DEBUG_BREAKPOINT_AT_WRITE_TO_ADDRESS_IGNORE 3 //The break at DEBUG_BREAKPOINT_AT_WRITE_TO_ADDRESS will be ignored n times.
//#define DEBUG_BREAKPOINT_AT_READ_FROM_ADDRESS 0xa007 //Trigger a breakpoint if data is read from a specific address
//#define DEBUG_BREAKPOINT_AT_READ_FROM_ADDRESS_IGNORE 0 //The break at DEBUG_BREAKPOINT_AT_READ_FROM_ADDRESS will be ignored n times.
//#define DEBUG_LOG_REGISTERS //Log register values in the history, this takes a few cycles from the critical rp2040 core and might cause PIO stall problems. Note, that for some reason I don't understand this messes badly with vsync.

#ifdef DEBUG_BREAKPOINT_AT_ADDRESS
    #define DEBUG_TRIGGER_BREAKPOINT_AT_ADDRESS \
            static uint debug_breakpoint_at_address_counter = DEBUG_BREAKPOINT_AT_ADDRESS_IGNORE; \
        if (*address == DEBUG_BREAKPOINT_AT_ADDRESS) { \
            if (debug_breakpoint_at_address_counter) { \
                debug_breakpoint_at_address_counter--; \
            } else { \
                running = false; \
                error = "Address breakpoint."; \
            } \
        }
#else
    #define DEBUG_TRIGGER_BREAKPOINT_AT_ADDRESS
#endif

#ifdef DEBUG_BREAKPOINT_AT_WRITE_TO_ADDRESS
    #define DEBUG_TRIGGER_BREAKPOINT_AT_WRITE_TO_ADDRESS \
            static uint debug_breakpoint_at_write_to_address_counter = DEBUG_BREAKPOINT_AT_WRITE_TO_ADDRESS_IGNORE; \
        if (address == DEBUG_BREAKPOINT_AT_WRITE_TO_ADDRESS) { \
            if (debug_breakpoint_at_write_to_address_counter) { \
                debug_breakpoint_at_write_to_address_counter--; \
            } else { \
                running = false; \
                error = "Write breakpoint."; \
            } \
        }
#else
    #define DEBUG_TRIGGER_BREAKPOINT_AT_WRITE_TO_ADDRESS
#endif

#ifdef DEBUG_BREAKPOINT_AT_READ_FROM_ADDRESS
    #define DEBUG_TRIGGER_BREAKPOINT_AT_READ_FROM_ADDRESS \
            static uint debug_breakpoint_at_read_from_address_counter = DEBUG_BREAKPOINT_AT_READ_FROM_ADDRESS_IGNORE; \
        if (*address == DEBUG_BREAKPOINT_AT_READ_FROM_ADDRESS) { \
            if (debug_breakpoint_at_read_from_address_counter) { \
                debug_breakpoint_at_read_from_address_counter--; \
            } else { \
                running = false; \
                error = "Read breakpoint."; \
            } \
        }
#else
    #define DEBUG_TRIGGER_BREAKPOINT_AT_READ_FROM_ADDRESS
#endif


#ifdef DEBUG_PPU_TIMING
extern bool recordPPUTiming, recordPPUTimingStarted, ppuTimingReady;

extern uint ppuTiming[LINES][4];

struct PPUTimingEvents {
    uint frameStartCycle;
    uint frameEndCycle;
    int vblankOffset;
};

extern struct PPUTimingEvents ppuTimingEvents;

void printPPUTiming();

#define DEBUG_MARK_YRESET \
    if (recordPPUTiming) { \
        if (recordPPUTimingStarted) { \
            ppuTimingEvents.frameEndCycle = cycleIndex; \
            recordPPUTiming = false; \
            recordPPUTimingStarted = false; \
            ppuTimingReady = true; \
        } else { \
            recordPPUTimingStarted = true; \
            ppuTimingEvents.vblankOffset = 0; \
            ppuTimingEvents.frameStartCycle = cycleIndex; \
            for (int i = 0; i < LINES; i++) { \
                ppuTiming[i][0] = 0; \
                ppuTiming[i][1] = 0; \
                ppuTiming[i][2] = 0; \
                ppuTiming[i][3] = 0; \
            } \
        } \
    }

#define DEBUG_MARK_OAMSEARCHSTART \
    if (recordPPUTiming) { \
        ppuTiming[y][0] = lineCycle; \
    }

#define DEBUG_MARK_OAMSEARCHSTOP \
    if (recordPPUTiming) { \
        if (ppuTiming[y][1] == 0) \
            ppuTiming[y][1] = lineCycle; \
    }

#define DEBUG_MARK_RENDERSTART \
    if (recordPPUTiming) { \
        ppuTiming[y][2] = lineCycle; \
    }

#define DEBUG_MARK_RENDERSTOP \
    if (recordPPUTiming) { \
        ppuTiming[y][3] = lineCycle; \
    }

#define DEBUG_MARK_VBLANK_ADJUST \
    if (recordPPUTiming) { \
        ppuTimingEvents.vblankOffset = vblankOffset; \
    }

#else

#define DEBUG_MARK_YRESET
#define DEBUG_MARK_OAMSEARCHSTART
#define DEBUG_MARK_OAMSEARCHSTOP
#define DEBUG_MARK_RENDERSTART
#define DEBUG_MARK_RENDERSTOP
#define DEBUG_MARK_VBLANK_ADJUST

#endif

#ifdef DEBUG_LOG_REGISTERS
    extern uint32_t volatile registerHistory32[64][2]; //We only log the last 64 events to save memory
    extern uint16_t volatile spHistory[64];
    extern uint32_t volatile flagHistory[64];
    #define DEBUG_TRIGGER_LOG_REGISTERS \
        registerHistory32[*historyIndex & 0x3f][0] = *((uint32_t *)(&registers[0])); \
        registerHistory32[*historyIndex & 0x3f][1] = *((uint32_t *)(&registers[4])); \
        spHistory[*historyIndex & 0x3f] = sp; \
        flagHistory[*historyIndex & 0x3f] = flags;

#else

    #define DEBUG_TRIGGER_LOG_REGISTERS

#endif

void dumpMemory();
void dumpBus();

#endif