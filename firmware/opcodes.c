#include "opcodes.h"

#include "cpubus.h"
#include "ppu.h"
#include "debug.h"


bool syncArmed = false;
uint statSyncStage = 0; //0 = false, 1 = register read to A, 2 = masked to 0x03, 3 = cp to 0x01
uint lySyncStage = 0; //0 = false, 1 = register read, 2 = cp
uint syncReferenceCycle;
int syncOffset;

//Helper to calculate offset to scanline for synching

void setOffsetToLine(uint8_t line) {
    syncOffset = (line - y) * CYCLES_PER_LINE - lineCycle;
    if (syncOffset > CYCLES_PER_FRAME/2)
        syncOffset -= CYCLES_PER_FRAME;
    else if (syncOffset < -CYCLES_PER_FRAME/2)
        syncOffset += CYCLES_PER_FRAME;
}

//Wrap memory writes to capture writes to registers

void toMemory(uint16_t address, uint8_t data) {
    DEBUG_TRIGGER_BREAKPOINT_AT_WRITE_TO_ADDRESS
    memory[address] = data;
    if (address >= 0xff00) {
        switch (address) {
            case 0xff04: //Reset DIV register
                *div = 0;
                break;
            case 0xff40: //LCDC
                bgAndWindowDisplay = (data & 0x01) != 0;
                objEnable = (data & 0x02) != 0;
                objSize = ((data & 0x04) != 0 ? 16 : 8);
                bgTileMap9C00 = (data & 0x08) != 0;
                tileData8000 = (data & 0x10) != 0;
                windowEnable = (data & 0x20) != 0;
                windowTileMap9C00 = (data & 0x40) != 0;
                lcdAndPpuEnable = (data & 0x80) != 0;
                break;
            case 0xff46: //OAM DMA transfer
                if (data >= 0x80) {
                    dmaToOAM((uint16_t)(data) << 8);
                    ignoreCycles = 160;
                } else {
                    error = "OAM DMA from cartridge not implemented.";
                    running = false;
                }
                break;
            case 0xff47: //BG Palette
                paletteBG[0] = ~((data & 0x03) * contrastFactor);
                paletteBG[1] = ~(((data >> 2) & 0x03) * contrastFactor);
                paletteBG[2] = ~(((data >> 4) & 0x03) * contrastFactor);
                paletteBG[3] = ~(((data >> 6) & 0x03) * contrastFactor);
                break;
            case 0xff48: //OBP0 Palette
                //Lowest bit is transparent and therefore ignored
                paletteOBP0[1] = ~(((data >> 2) & 0x03) * contrastFactor);
                paletteOBP0[2] = ~(((data >> 4) & 0x03) * contrastFactor);
                paletteOBP0[3] = ~(((data >> 6) & 0x03) * contrastFactor);
                break;
            case 0xff49: //OBP1 Palette
                //Lowest bit is transparent and therefore ignored
                paletteOBP1[1] = ~(((data >> 2) & 0x03) * contrastFactor);
                paletteOBP1[2] = ~(((data >> 4) & 0x03) * contrastFactor);
                paletteOBP1[3] = ~(((data >> 6) & 0x03) * contrastFactor);
                break;
        }
    }
}

//Read from memory, memory substitutions are already done in getNextFromBus, so this is mostly an inline replacement for debugging and some special registers
//The caller has to make sure to advance the bus to the relevant position before calling from memory if data from the cartridge is to be expected.

uint8_t static inline fromMemory() {
    DEBUG_TRIGGER_BREAKPOINT_AT_READ_FROM_ADDRESS
    switch (*address) {
        case 0xff04: return *div; //DIV register
        case 0xff41:
                    //STAT register. Since this is usually only used for conditional jumps done in the real Game Boy, emulating the correct value is not ciritcally here.
                    //Instead we use it to synchronize our PPU to the real one:
                    //We need to understand if the game reads STAT in a tight loop to enter some critical code with extremely precise timing and we need to synchronize our PPU to it.
                    //However, since mode 0, 2 and 3 occur multiple time throughout a frame and only help if we already have a good synch, we only look at loops that wait for mode 1 (vblank).
                    //This has a one specific kind of loop in mind: Read STAT into A, mask the mode bit (0x03), compare to the mode we are waiting for and then conditionally jump back if the result is non-zero
                    //Honestly, this is just what I did for my Wifi cartridge and I am not sure if it is that common to do. But so far all other games were synched well enough through the vsync interrupt and a tight loop waiting for LY
                    syncArmed = true;
                    statSyncStage = 1;
                    lySyncStage = 0;
                    syncReferenceCycle = cycleIndex;
                    if (y >= SCREEN_H)
                        return 1;
                    else switch (renderState) {
                        case rendering: return 3;
                        case done: return 0;
                        default: return 2;
                    }
        case 0xff44:
                    //LY register. The exact value usually is not critical as the Game Boy will usually only use this for conditional jumps, but we can return our PPU y position here anyway.
                    //We can assume that the game reads LY in a tight loop to enter some critical code with extremely precise timing and we need to synchronize our PPU to it.
                    //This is a very naive approach with DOnkey Kong Land in mind:
                    //LY is read periodically and compared to a for a nz jump. We can probably assume that LY will always be compared to a and that it makes sense for a tight loop to load the value for a before entering the loop.
                    //So, we remember the value of a here as the y coordinate at which our PPU should be now and note the difference to the actual ppu cycle. We then use a later JR jump (within three cycles) if it is not taken to apply the difference as vsync correction.
                    syncArmed = true;
                    statSyncStage = 0;
                    lySyncStage = 1;
                    syncReferenceCycle = cycleIndex;
                    return y;
    }
    return *opcode;
}

// Various forms of NOOP for opcodes that we do not need to track in detail //

void noop1() {
    getNextFromBus();
}

void noop3() {
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
}

void noop4() {
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
}

void noop2_3() {
    uint16_t nextPC = *address + 2;
    getNextFromBus();
    getNextFromBus();
    if (nextPC != *address) { //If these are equal, a jump was not taken but the next code was fetched.
        getNextFromBus();               //If not equal, burn another cycle.
    }
}

void noop3_4() {
    uint16_t nextPC = *address + 3;
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
    if (nextPC != *address) //If these are equal, a jump was not taken but the next code was fetched.
        getNextFromBus();               //If not equal, burn another cycle.
}

// ADD //

void add_A_d8() {
    getNextFromBus();
    uint8_t d8 = *opcode;
    *N = 0;
    *H = (((*a & 0x0f) + (d8 & 0x0f)) >= 0x10);
    *C = (((uint16_t)(*a) + (uint16_t)d8) >= 0x0100);
    *a += d8;
    *Z = (*a == 0);
    getNextFromBus();
}

void adc_A_d8() {
    uint8_t cy;
    if (*C)
        cy = 1;
    else
        cy = 0;
    getNextFromBus();
    uint8_t d8 = *opcode;
    *N = 0;
    *H = (((*a & 0x0f) + (d8 & 0x0f) + cy) >= 0x10);
    *C = (((uint16_t)(*a) + (uint16_t)d8 + cy) >= 0x0100);
    *a += d8 + cy;
    *Z = (*a == 0);
    getNextFromBus();
}

#define GENERATE_ADD_A_R(REGISTER) \
void add_A_ ## REGISTER() { \
    *N = 0; \
    *H = (((*a & 0x0f) + (*REGISTER & 0x0f)) >= 0x10); \
    *C = (((uint16_t)(*a) + (uint16_t)*REGISTER) >= 0x0100); \
    *a += *REGISTER; \
    *Z = (*a == 0); \
    getNextFromBus(); \
}

GENERATE_ADD_A_R(b)
GENERATE_ADD_A_R(c)
GENERATE_ADD_A_R(d)
GENERATE_ADD_A_R(e)
GENERATE_ADD_A_R(h)
GENERATE_ADD_A_R(l)
GENERATE_ADD_A_R(a)

void add_A_HL() {
    getNextFromBus();
    uint8_t v = fromMemory();
    *N = 0;
    *H = (((*a & 0x0f) + (v & 0x0f)) >= 0x10);
    *C = (((uint16_t)(*a) + (uint16_t)v) >= 0x0100);
    *a += v;
    *Z = (*a == 0);
    getNextFromBus();
}

#define GENERATE_ADC_A_R(REGISTER) \
void adc_A_ ## REGISTER() { \
    uint8_t cy; \
    if (*C) \
        cy = 1; \
    else \
        cy = 0; \
    *N = 0; \
    *H = (((*a & 0x0f) + (*REGISTER & 0x0f) + cy) >= 0x10); \
    *C = (((uint16_t)(*a) + (uint16_t)*REGISTER + cy) >= 0x0100); \
    *a += *REGISTER + cy; \
    *Z = (*a == 0); \
    getNextFromBus(); \
}

GENERATE_ADC_A_R(b)
GENERATE_ADC_A_R(c)
GENERATE_ADC_A_R(d)
GENERATE_ADC_A_R(e)
GENERATE_ADC_A_R(h)
GENERATE_ADC_A_R(l)
GENERATE_ADC_A_R(a)

void adc_A_HL() {
    uint8_t cy;
    if (*C)
        cy = 1;
    else
        cy = 0;
    getNextFromBus();
    uint8_t v = fromMemory();
    *N = 0;
    *H = (((*a & 0x0f) + (v & 0x0f) + cy) >= 0x10);
    *C = (((uint16_t)(*a) + (uint16_t)v + cy) >= 0x0100);
    *a += v + cy;
    *Z = (*a == 0);
    getNextFromBus();
}

void add_HL_r() {
    uint16_t r16;
    switch (rawBusData & 0x00300000) {
        case 0x00000000:
            r16 = *bc;
            break;
        case 0x00100000:
            r16 = *de;
            break;
        case 0x00200000:
            r16 = *hl;
            break;
        case 0x00300000:
            r16 = sp;
            break;
    }
    *N = 0;
    *C = (((uint32_t)*hl + (uint32_t)r16) >= 0x010000);
    *H = (((*hl & 0x0fff) + (r16 & 0x0fff)) >= 0x1000);
    *hl += r16;
    getNextFromBus();
    getNextFromBus();
}

void add_SP_s8() {
    getNextFromBus();
    int8_t s8 = *opcode;
    flags = 0x00000000;
    *H = (((sp & 0x0f) + (s8 & 0x0f)) >= 0x10);
    *C = (((sp & 0xff) + (int16_t)s8) >= 0x0100);
    sp += s8;
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
}

// AND //

#define GENERATE_AND_R(REGISTER) \
void and_ ## REGISTER() { \
    *a &= *REGISTER; \
    flags = 0x00000100; \
    *Z = (*a == 0); \
    if (syncArmed) { \
        if (statSyncStage == 1 && *REGISTER == 0x03) \
            statSyncStage = 2; \
        else \
            syncArmed = false; \
    } \
    getNextFromBus(); \
}

GENERATE_AND_R(b)
GENERATE_AND_R(c)
GENERATE_AND_R(d)
GENERATE_AND_R(e)
GENERATE_AND_R(h)
GENERATE_AND_R(l)
GENERATE_AND_R(a)

void and_HL() {
    getNextFromBus();
    *a &= fromMemory();
    flags = 0x00000100;
    *Z = (*a == 0);
    getNextFromBus();
}

void and_d8() {
    getNextFromBus();
    uint8_t d8 = *opcode;
    *a &= d8;
    flags = 0x00000100;
    *Z = (*a == 0);
    if (syncArmed) {
        if (statSyncStage == 1 && d8 == 0x03)
            statSyncStage = 2;
        else
            syncArmed = false;
    } \
    getNextFromBus();
}

// CALL //

void call6() {
    toMemory(--sp, *address >> 8);
    toMemory(--sp, *address);
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
    if (*address != sp) {
        running = false;
        error = "SP desynchronized.";
    }
    getNextFromBus();
}

void call3_6() {
    uint16_t addr = *address;
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
    if (addr + 3 != *address) {          //If these are equal, a jump was not taken but the next code was fetched.
         //If not equal, burn three more cycles and push to sp register
        toMemory(--sp, addr >> 8);
        toMemory(--sp, addr);
        getNextFromBus();                 
        getNextFromBus();
        if (*address != sp) {
            running = false;
            error = "SP desynchronized.";
        }
        getNextFromBus();
    }
}

// CCF //

void ccf() {
    *N = 0;
    *H = 0;
    *C = !*C;
    getNextFromBus();
}

// CP //

#define GENERATE_CP_R(REGISTER) \
void cp_ ## REGISTER() { \
    *N = 1; \
    *Z = (*a == *REGISTER); \
    *H = ((*a & 0x0f) < (*REGISTER & 0x0f)); \
    *C = (*a < *REGISTER); \
    \
    if (syncArmed) { \
        if (statSyncStage == 2) { \
            if (*REGISTER == 0x01) { \
                setOffsetToLine(144); \
                statSyncStage = 3; \
            } else \
                syncArmed = false; \
        } else if (lySyncStage == 1) { \
            setOffsetToLine(*REGISTER); \
            lySyncStage = 2; \
        } else \
            syncArmed = false; \
    } \
    getNextFromBus(); \
}

GENERATE_CP_R(b)
GENERATE_CP_R(c)
GENERATE_CP_R(d)
GENERATE_CP_R(e)
GENERATE_CP_R(h)
GENERATE_CP_R(l)
GENERATE_CP_R(a)

void cp_HL() {
    getNextFromBus();
    uint8_t v = fromMemory();
    *N = 1;
    *Z = (*a == v);
    *H = ((*a & 0x0f) < (v & 0x0f));
    *C = (*a < v);
    getNextFromBus();
}

void cp_d8() {
    getNextFromBus();
    uint8_t d8 = *opcode;
    *N = 1;
    *Z = (*a == d8);
    *H = ((*a & 0x0f) < (d8 & 0x0f));
    *C = (*a < d8);
    if (syncArmed) { \
        if (statSyncStage == 2) {
            if (d8 == 0x01) {
                setOffsetToLine(144);
                statSyncStage = 3;
            } else
                syncArmed = false;
        } else if (lySyncStage == 1) {
            setOffsetToLine(d8);
            lySyncStage = 2;
        } else
            syncArmed = false;
    }
    getNextFromBus();
}

// CPL //

void cpl() {
    *a = ~*a;
    *H = 1;
    *N = 1;
    getNextFromBus();
}

// DAA //

void daa() {
    if (*N) { //Result of subtraction
        if (*C)
            *a -= 0x60;
        if (*H)
            *a -= 0x06;
    } else {
        if (*C || *a > 0x99) {
            *a += 0x60;
            *C = 1;
        }
        if (*H || (*a & 0x0f) > 0x09) {
            *a += 0x06;
        }
    }
    *H = false;
    *Z = (*a == 0);
    getNextFromBus();
}

// DEC //

#define GENERATE_DEC_R(REGISTER) \
void dec_ ## REGISTER() { \
    *N = 1; \
    (*REGISTER)--; \
    *H = (*REGISTER & 0x0f == 0x0f); \
    *Z = (*REGISTER == 0x00); \
    getNextFromBus(); \
}

GENERATE_DEC_R(b)
GENERATE_DEC_R(c)
GENERATE_DEC_R(d)
GENERATE_DEC_R(e)
GENERATE_DEC_R(h)
GENERATE_DEC_R(l)
GENERATE_DEC_R(a)

void dec_HL() {
    *N = 1;
    getNextFromBus();
    uint8_t data = fromMemory()-1;
    toMemory(*hl, data);
    *H = (data & 0x0f == 0x0f);
    *Z = (data == 0x00);
    getNextFromBus();
    getNextFromBus();
}

void dec_r16() {
    switch (rawBusData & 0x00300000) {
        case 0x00000000:
            (*bc)--;
            break;
        case 0x00100000:
            (*de)--;
            break;
        case 0x00200000:
            (*hl)--;
            break;
        case 0x00300000:
            sp--;
            break;
    }
    getNextFromBus();
    getNextFromBus();
}

// DI/EI

void di() {
    interruptsEnabled = false;
    getNextFromBus();
}

void ei() {
    getNextFromBus();
    interruptsEnabled = true;
    interruptsEnableCycle = cycleIndex;
}

// HALT //

void halt() {
    getNextFromBus();
    if ((uint16_t)history[*historyIndex+1] == *address) //We often see the next command twice. If the clock is actually suspended, getNextFromBus deals with this issue, but if the halt is too short or not even executed, we need to get rid of the duplicate here. There is no reason to repeat the address for an opcode, so it is save to remove it here.
        getNextFromBus();
}

// INC //

#define GENERATE_INC_R(REGISTER)  \
void inc_ ## REGISTER() { \
    *N = 0; \
    (*REGISTER)++; \
    *H = (*REGISTER & 0x0f == 0x00); \
    *Z = (*REGISTER == 0x00); \
    getNextFromBus(); \
}

GENERATE_INC_R(b)
GENERATE_INC_R(c)
GENERATE_INC_R(d)
GENERATE_INC_R(e)
GENERATE_INC_R(h)
GENERATE_INC_R(l)
GENERATE_INC_R(a)

void inc_HL() {
    *N = 0;
    getNextFromBus();
    uint8_t data = fromMemory()+1;
    toMemory(*hl, data);
    *H = (data & 0x0f == 0x00);
    *Z = (data == 0x00);
    getNextFromBus();
    getNextFromBus();
}

void inc_r16() {
    switch (rawBusData & 0x00300000) {
        case 0x00000000:
            (*bc)++;
            break;
        case 0x00100000:
            (*de)++;
            break;
        case 0x00200000:
            (*hl)++;
            break;
        case 0x00300000:
            sp++;
            break;
    }
    getNextFromBus();
    getNextFromBus();
}

// JR //

void jr_nz() {
    //For us jumps are mostly NOPs because we simply follow the PC of the real Game Boy and don't have to make any decisions ourselves. But in conditional relative jumps are sometimes part of a tight loop waiting for a specific PPU state, which we need to use to synchronize our PPU
    uint16_t nextPC = *address + 2;
    getNextFromBus();
    getNextFromBus();
    if (nextPC != *address) { //If these are equal, a jump was not taken but the next code was fetched.
        getNextFromBus();               //If not equal, burn another cycle.
    } else if (syncArmed) {
        //At this point the jump was not taken and we can assume that the condition we have been waiting for was finally found.
        if (statSyncStage == 3) {
            if (cycleIndex - syncReferenceCycle < 9) { //The JR did not occure more than 7 cycles later - anything else was not a tight loop and something more complex that we cannot handle
                vblankOffset = syncOffset;
            }
        } else if (lySyncStage == 2) {
            if (cycleIndex - syncReferenceCycle < 7) { //The JR did not occure more than 5 cycles later - anything else was not a tight loop and something more complex that we cannot handle
                vblankOffset = syncOffset;
            }
        }
        syncArmed = false;
    }
}

void jr_z() {
    //See jr_nz
    uint16_t nextPC = *address + 2;
    getNextFromBus();
    getNextFromBus();
    if (nextPC != *address) { //If these are equal, a jump was not taken but the next code was fetched.
        if (syncArmed) {
            //At this point the jump was taken and we can assume that the condition we have been waiting for was finally found.
            if (cycleIndex - syncReferenceCycle < 7) { //The JR did not occure more than 5 cycles later - anything else was not a tight loop and something more complex that we cannot handle
            if (statSyncStage == 3 || lySyncStage == 2) {
                vblankOffset = syncOffset;
            }
        }
            syncArmed = false;
        }
        getNextFromBus();
    }
}

// LD //

void ld_A_a8() {
    getNextFromBus();
    uint8_t a8 = *opcode;
    getNextFromBus();
    *a = fromMemory();
    getNextFromBus();
}

void ld_a8_A() {
    getNextFromBus();
    uint8_t a8 = *opcode;
    toMemory(0xff00 | a8, *a);
    getNextFromBus();
    getNextFromBus();
}

void ld_A_aC() {
    getNextFromBus();
    *a = fromMemory();
    getNextFromBus();
}

void ld_aC_A() {
    toMemory(0xff00 | *c, *a);
    getNextFromBus();
    getNextFromBus();
}

void ld_A_a16() {
    getNextFromBus();
    uint16_t a16 = (uint8_t)(rawBusData >> 16);
    getNextFromBus();
    a16 |= ((rawBusData >> 8) & 0xff00);
    getNextFromBus();
    *a = fromMemory();
    getNextFromBus();
}

void ld_a16_A() {
    getNextFromBus();
    uint16_t a16 = (uint8_t)(rawBusData >> 16);
    getNextFromBus();
    a16 |= ((rawBusData >> 8) & 0xff00);
    toMemory(a16, *a);
    getNextFromBus();
    getNextFromBus();
}

void ld_a16_SP() {
    getNextFromBus();
    uint16_t a16 = (uint8_t)(rawBusData >> 16);
    getNextFromBus();
    a16 |= ((rawBusData >> 8) & 0xff00);
    toMemory(a16, (uint8_t)sp);
    toMemory(a16+1, (sp >> 8));
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
}

void ld_HL_SPs8() {
    getNextFromBus();
    int8_t s8 = *opcode;
    flags = 0x00000000;
    *H = (((sp & 0x0f) + (s8 & 0x0f)) >= 0x10);
    *C = (((sp & 0xff) + (int16_t)s8) >= 0x0100);
    *hl = sp + s8;
    getNextFromBus();
    getNextFromBus();
}

void ld_mem_A() { //ld (bc) , A; ld (de), A; ld (hl+), A; ld (hl-), A
    switch (rawBusData & 0x00300000) {
        case 0x00000000:
            toMemory(*bc, *a);
            break;
        case 0x00100000:
            toMemory(*de, *a);
            break;
        case 0x00200000:
            toMemory((*hl)++, *a);
            break;
        case 0x00300000:
            toMemory((*hl)--, *a);
            break;
    }
    getNextFromBus();
    getNextFromBus();
}

void ld_A_mem() { //ld A, (bc); ld A, (de); ld A, (hl+); ld A, (hl-)
    switch (rawBusData & 0x00300000) {
        case 0x00200000:
            (*hl)++;
            break;
        case 0x00300000:
            (*hl)--;
            break;
    }
    getNextFromBus();
    *a = fromMemory();
    getNextFromBus();
}

void ld_r_d16() {
    uint32_t target = rawBusData & 0x00300000;
    getNextFromBus();
    uint16_t v = (uint8_t)(rawBusData >> 16);
    getNextFromBus();
    v |= ((rawBusData >> 8) & 0xff00);
    
    switch (target) {
        case 0x00000000:
            *bc = v;
            break;
        case 0x00100000:
            *de = v;
            break;
        case 0x00200000:
            *hl = v;
            break;
        case 0x00300000:
            sp = v;
            break;
    }
    getNextFromBus();
}

#define GENERATE_LD_R_R(REGISTER1, REGISTER2) \
void ld_ ## REGISTER1 ## _ ## REGISTER2() { \
    *REGISTER1 = *REGISTER2; \
    getNextFromBus(); \
}

GENERATE_LD_R_R(b, b)
GENERATE_LD_R_R(b, c)
GENERATE_LD_R_R(b, d)
GENERATE_LD_R_R(b, e)
GENERATE_LD_R_R(b, h)
GENERATE_LD_R_R(b, l)
GENERATE_LD_R_R(b, a)
GENERATE_LD_R_R(c, b)
GENERATE_LD_R_R(c, c)
GENERATE_LD_R_R(c, d)
GENERATE_LD_R_R(c, e)
GENERATE_LD_R_R(c, h)
GENERATE_LD_R_R(c, l)
GENERATE_LD_R_R(c, a)
GENERATE_LD_R_R(d, b)
GENERATE_LD_R_R(d, c)
GENERATE_LD_R_R(d, d)
GENERATE_LD_R_R(d, e)
GENERATE_LD_R_R(d, h)
GENERATE_LD_R_R(d, l)
GENERATE_LD_R_R(d, a)
GENERATE_LD_R_R(e, b)
GENERATE_LD_R_R(e, c)
GENERATE_LD_R_R(e, d)
GENERATE_LD_R_R(e, e)
GENERATE_LD_R_R(e, h)
GENERATE_LD_R_R(e, l)
GENERATE_LD_R_R(e, a)
GENERATE_LD_R_R(h, b)
GENERATE_LD_R_R(h, c)
GENERATE_LD_R_R(h, d)
GENERATE_LD_R_R(h, e)
GENERATE_LD_R_R(h, h)
GENERATE_LD_R_R(h, l)
GENERATE_LD_R_R(h, a)
GENERATE_LD_R_R(l, b)
GENERATE_LD_R_R(l, c)
GENERATE_LD_R_R(l, d)
GENERATE_LD_R_R(l, e)
GENERATE_LD_R_R(l, h)
GENERATE_LD_R_R(l, l)
GENERATE_LD_R_R(l, a)
GENERATE_LD_R_R(a, b)
GENERATE_LD_R_R(a, c)
GENERATE_LD_R_R(a, d)
GENERATE_LD_R_R(a, e)
GENERATE_LD_R_R(a, h)
GENERATE_LD_R_R(a, l)
GENERATE_LD_R_R(a, a)

#define GENERATE_LD_R_HL(REGISTER) \
void ld_ ## REGISTER ## _HL() { \
    getNextFromBus(); \
    *REGISTER = fromMemory(); \
    getNextFromBus(); \
}

GENERATE_LD_R_HL(b)
GENERATE_LD_R_HL(c)
GENERATE_LD_R_HL(d)
GENERATE_LD_R_HL(e)
GENERATE_LD_R_HL(h)
GENERATE_LD_R_HL(l)
GENERATE_LD_R_HL(a)

#define GENERATE_LD_HL_R(REGISTER) \
void ld_HL_ ## REGISTER() { \
    toMemory(*hl, *REGISTER); \
    getNextFromBus(); \
    getNextFromBus(); \
}

GENERATE_LD_HL_R(b)
GENERATE_LD_HL_R(c)
GENERATE_LD_HL_R(d)
GENERATE_LD_HL_R(e)
GENERATE_LD_HL_R(h)
GENERATE_LD_HL_R(l)
GENERATE_LD_HL_R(a)

#define GENERATE_LD_R_D8(REGISTER) \
void ld_ ## REGISTER ## _d8() { \
    getNextFromBus(); \
    uint8_t v = *opcode; \
    *REGISTER = v; \
    getNextFromBus(); \
}

GENERATE_LD_R_D8(b)
GENERATE_LD_R_D8(c)
GENERATE_LD_R_D8(d)
GENERATE_LD_R_D8(e)
GENERATE_LD_R_D8(h)
GENERATE_LD_R_D8(l)
GENERATE_LD_R_D8(a)

void ld_HL_d8() {
    getNextFromBus();
    uint8_t v = *opcode;
    toMemory(*hl, v);
    getNextFromBus();
    getNextFromBus();
}

void ld_SP_HL() {
    sp = *hl;
    getNextFromBus();
    getNextFromBus();
}

// OR //

#define GENERATE_OR_R(REGISTER) \
void or_ ## REGISTER() { \
    *a |= *REGISTER; \
    flags = 0x00000000; \
    *Z = (*a == 0); \
    getNextFromBus(); \
}

GENERATE_OR_R(b)
GENERATE_OR_R(c)
GENERATE_OR_R(d)
GENERATE_OR_R(e)
GENERATE_OR_R(h)
GENERATE_OR_R(l)
GENERATE_OR_R(a)

void or_HL() {
    getNextFromBus();
    *a |= fromMemory();
    flags = 0x00000000;
    *Z = (*a == 0);
    getNextFromBus();
}

void or_d8() {
    getNextFromBus();
    uint8_t d8 = *opcode;
    *a |= d8;
    flags = 0x00000000;
    *Z = (*a == 0);
    getNextFromBus();
}

// POP //

void pop_r16() {
    uint32_t whichOpcode = rawBusData & 0x00300000;
    getNextFromBus();
    if (*address != sp) {
        running = false;
        error = "SP desynchronized.";
    }
    uint16_t v = fromMemory();
    sp++;
    getNextFromBus();
    v |= ((uint16_t)fromMemory() << 8);
    sp++;
    switch (whichOpcode) {
        case 0x00000000:
            *bc = v;
            break;
        case 0x00100000:
            *de = v;
            break;
        case 0x00200000:
            *hl = v;
            break;
        case 0x00300000:
            *a = (v >> 8);
            *Z = ((v & 0x0080) != 0);
            *N = ((v & 0x0040) != 0);
            *H = ((v & 0x0020) != 0);
            *C = ((v & 0x0010) != 0);
            break;
    }
    getNextFromBus();
}

// PUSH //

void push_r16() {
    uint16_t v;
    switch (rawBusData & 0x00300000) {
        case 0x00000000:
            v = *bc;
            break;
        case 0x00100000:
            v = *de;
            break;
        case 0x00200000:
            v = *hl;
            break;
        case 0x00300000:
            v = ((uint16_t)*a << 8) | (*Z ? 0x0080 : 0x0000) | (*N ? 0x0040 : 0x0000) | (*H ? 0x0020 : 0x0000) | (*C ? 0x0010 : 0x0000);
            break;
    }
    toMemory(--sp, (v >> 8));
    toMemory(--sp, (uint8_t)v);
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
    if (*address != sp) {
        running = false;
        error = "SP desynchronized.";
    }
    getNextFromBus();
}

// RET //

void ret4() {
    getNextFromBus();
    if (*address != sp) {
        running = false;
        error = "SP desynchronized.";
    }
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
    sp += 2;
}

void reti4() {
    getNextFromBus();
    if (*address != sp) {
        running = false;
        error = "SP desynchronized.";
    }
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
    sp += 2;
    interruptsEnabled = true;
    interruptsEnableCycle = cycleIndex;
}

void ret2_5() {
    uint16_t nextPC = *address + 1;
    getNextFromBus();
    getNextFromBus();
    if (nextPC != *address) { //If these are equal, a jump was not taken but the next code was fetched.
        //If not equal, burn three more cycles and pop the sp register.
        if (*address != sp) {
            running = false;
            error = "SP desynchronized.";
        }
        getNextFromBus();
        getNextFromBus();
        getNextFromBus();
        sp += 2;
    }
}

// RLCA, RLA //

void rla() {
    bool carry = *C;
    flags = 0x00000000;
    *C = ((*a & 0x80) != 0);
    *a <<= 1;
    if (carry)
        *a |= 0x01;
    getNextFromBus();
}

void rlca() {
    flags = 0x00000000;
    *C = ((*a & 0x80) != 0);
    *a <<= 1;
    if (*C)
        *a |= 0x01;
    getNextFromBus();
}

// RRCA, RRA //

void rra() {
    bool carry = *C;
    flags = 0x00000000;
    *C = ((*a & 0x01) != 0);
    *a >>= 1;
    if (carry)
        *a |= 0x80;
    getNextFromBus();
}

void rrca() {
    flags = 0x00000000;
    *C = ((*a & 0x01) != 0);
    *a >>= 1;
    if (*C)
        *a |= 0x80;
    getNextFromBus();
}

// RST //

void rst() {
    toMemory(--sp, *address >> 8);
    toMemory(--sp, *address);
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
    getNextFromBus();
}

// SCF //

void scf() {
    *N = 0;
    *H = 0;
    *C = 1;
    getNextFromBus();
}

// SUB //

void sub_A_d8() {
    getNextFromBus();
    uint8_t d8 = *opcode;
    *N = 1;
    *H = ((*a & 0x0f) < (d8 & 0x0f));
    *C = (*a < d8);
    *a -= d8;
    *Z = (*a == 0);
    getNextFromBus();
}

void sbc_A_d8() {
    uint8_t cy;
    if (*C)
        cy = 1;
    else
        cy = 0;
    getNextFromBus();
    uint8_t d8 = *opcode;
    *N = 1;
    *H = ((*a & 0x0f) < (d8 & 0x0f) - cy);
    *C = (*a < d8 - cy);
    *a -= d8 + cy;
    *Z = (*a == 0);
    getNextFromBus();
}

#define GENERATE_SUB_A_R(REGISTER) \
void sub_A_ ## REGISTER() { \
    *N = 1; \
    *H = ((*a & 0x0f) < (*REGISTER & 0x0f)); \
    *C = (*a < *REGISTER); \
    *a -= *REGISTER; \
    *Z = (*a == 0); \
    getNextFromBus(); \
}

GENERATE_SUB_A_R(b)
GENERATE_SUB_A_R(c)
GENERATE_SUB_A_R(d)
GENERATE_SUB_A_R(e)
GENERATE_SUB_A_R(h)
GENERATE_SUB_A_R(l)
GENERATE_SUB_A_R(a)

void sub_A_HL() {
    getNextFromBus();
    uint8_t v = fromMemory();
    *N = 1;
    *H = ((*a & 0x0f) < (v & 0x0f));
    *C = (*a < v);
    *a -= v;
    *Z = (*a == 0);
    getNextFromBus();
}

#define GENERATE_SBC_A_R(REGISTER) \
void sbc_A_ ## REGISTER() { \
    uint8_t cy; \
    if (*C) \
        cy = 1; \
    else \
        cy = 0; \
    *N = 1; \
    *H = ((*a & 0x0f) < (*REGISTER & 0x0f) - cy); \
    *C = (*a < *REGISTER - cy); \
    *a -= *REGISTER + cy; \
    *Z = (*a == 0); \
    getNextFromBus(); \
}

GENERATE_SBC_A_R(b)
GENERATE_SBC_A_R(c)
GENERATE_SBC_A_R(d)
GENERATE_SBC_A_R(e)
GENERATE_SBC_A_R(h)
GENERATE_SBC_A_R(l)
GENERATE_SBC_A_R(a)

void sbc_A_HL() {
    uint8_t cy;
    if (*C)
        cy = 1;
    else
        cy = 0;
    getNextFromBus();
    uint8_t v = fromMemory();
    *N = 1;
    *H = ((*a & 0x0f) < (v & 0x0f) - cy);
    *C = (*a < v - cy);
    *a -= v + cy;
    *Z = (*a == 0);
    getNextFromBus();
}

// XOR //

#define GENERATE_XOR_R(REGISTER) \
void xor_ ## REGISTER() { \
    *a ^= *REGISTER; \
    flags = 0x00000000; \
    *Z = (*a == 0); \
    getNextFromBus(); \
}

GENERATE_XOR_R(b)
GENERATE_XOR_R(c)
GENERATE_XOR_R(d)
GENERATE_XOR_R(e)
GENERATE_XOR_R(h)
GENERATE_XOR_R(l)
GENERATE_XOR_R(a)

void xor_HL() {
    getNextFromBus();
    *a ^= fromMemory();
    flags = 0x00000000;
    *Z = (*a == 0);
    getNextFromBus();
}

void xor_d8() {
    getNextFromBus();
    uint8_t d8 = *opcode;
    *a ^= d8;
    flags = 0x00000000;
    *Z = (*a == 0);
    getNextFromBus();
}

// 0xCB OPCODES //

void xCB() {
    getNextFromBus();
    uint8_t opcode = (uint8_t)(rawBusData >> 16);
    if (opcode & 0x80) {
        // SET and RES
        uint8_t bit = ((opcode & 0x38) >> 3);
        uint8_t i = (opcode & 0x07) ^ 0x01;
        if (i == 0x07) {
            getNextFromBus();
            if (opcode & 0x40) // SET bit, (HL)
                toMemory(*hl, fromMemory() | (1 << bit));
            else                // RES bit, (HL)
                toMemory(*hl, fromMemory() & ~(1 << bit));
            getNextFromBus();
        } else {
            if (opcode & 0x40) // SET bit, A
                registers[i] |= (1 << bit);
            else                // RES bit, A
                registers[i] &= ~(1 << bit);
        }
    } else if (opcode & 0x40) {
        // BIT
        uint8_t bit = ((opcode & 0x38) >> 3);
        *N = 0;
        *H = 1;
        uint8_t i = (opcode & 0x07) ^ 0x01;
        uint8_t v;
        if (i == 0x07) {
            getNextFromBus();
            v = fromMemory();
        } else {
            v = registers[i];
        }
        *Z = (((v >> bit) & 0x01) == 0);
    } else if (opcode & 0x20) {
        // SLA/SRA and SWAP/SRL
        if (opcode & 0x10) {
            //SWAP/SRL
            if (opcode & 0x08) {
                //SRL
                flags = 0x00000000;
                uint8_t i = (opcode & 0x07) ^ 0x01;
                if (i == 0x07) {
                    getNextFromBus();
                    uint8_t data = fromMemory();
                    *C = (data & 0x01) != 0;
                    data >>= 1;
                    toMemory(*hl, data);
                    *Z = (data == 0);
                    getNextFromBus();
                } else {
                    *C = (registers[i] & 0x01) != 0;
                    registers[i] >>= 1;
                    *Z = (registers[i] == 0);
                }
            } else {
                //SWAP
                flags = 0x00000000;
                uint8_t i = (opcode & 0x07) ^ 0x01;
                if (i == 0x07) {
                    getNextFromBus();
                    uint8_t data = fromMemory();
                    toMemory(*hl, ((data >> 4) | (data << 4)));
                    *Z = (data & 0x01) == 0;
                    getNextFromBus();
                } else {
                    registers[i] = ((registers[i] >> 4) | (registers[i] << 4));
                    *Z = (registers[i] == 0);
                }
            }
        } else {
            //SLA/SRA
            if (opcode & 0x08) {
                //SRA
                flags = 0x00000000;
                uint8_t i = (opcode & 0x07) ^ 0x01;
                if (i == 0x07) {
                    getNextFromBus();
                    uint8_t data = fromMemory();
                    *C = (data & 0x01) != 0;
                    if (data & 0x80)
                        data = ((data >> 1) | 0x80);
                    else
                        data >>= 1;
                    toMemory(*hl, data);
                    *Z = (data == 0);
                    getNextFromBus();
                } else {
                    *C = (registers[i] & 0x01) != 0;
                    if (registers[i] & 0x80)
                        registers[i] = ((registers[i] >> 1) | 0x80);
                    else
                        registers[i] >>= 1;
                    *Z = (registers[i] == 0);
                }
            } else {
                //SLA
                flags = 0x00000000;
                uint8_t i = (opcode & 0x07) ^ 0x01;
                if (i == 0x07) {
                    getNextFromBus();
                    uint8_t data = fromMemory();
                    *C = (data & 0x80) != 0;
                    data <<= 1;
                    toMemory(*hl, data);
                    *Z = (data == 0);
                    getNextFromBus();
                } else {
                    *C = (registers[i] & 0x80) != 0;
                    registers[i] <<= 1;
                    *Z = (registers[i] == 0);
                }
            }
        }
    } else {
        //RL/RLC/RR/RRC
        if (opcode & 0x08) {
            //RR/RRC
            uint8_t i = (opcode & 0x07) ^ 0x01;
            if (opcode & 0x10) {
                //RR
                *N = 0;
                *H = 0;
                if (i == 0x07) {
                    getNextFromBus();
                    uint8_t data = fromMemory();
                    bool bit0 = ((data & 0x01) != 0);
                    data >>= 1;
                    if (*C)
                        data |= 0x80;
                    toMemory(*hl, data);
                    *C = bit0;
                    *Z = (data == 0);
                    getNextFromBus();
                } else {
                    bool bit0 = (registers[i] & 0x01) != 0;
                    registers[i] >>= 1;
                    if (*C)
                        registers[i] |= 0x80;
                    *C = bit0;
                    *Z = (registers[i] == 0);
                }
            } else {
                flags = 0x00000000;
                //RRC
                if (i == 0x07) {
                    getNextFromBus();
                    uint8_t data = fromMemory();
                    *C = ((data & 0x01) != 0);
                    data >>= 1;
                    if (*C)
                        data |= 0x80;
                    toMemory(*hl, data);
                    *Z = (data == 0);
                    getNextFromBus();
                } else {
                    *C = (registers[i] & 0x01) != 0;
                    registers[i] >>= 1;
                    if (*C)
                        registers[i] |= 0x80;
                    *Z = (registers[i] == 0);
                }
            }
        } else {
            //RL/RLC
            uint8_t i = (opcode & 0x07) ^ 0x01;
            if (opcode & 0x10) {
                //RL
                *N = 0;
                *H = 0;
                if (i == 0x07) {
                    getNextFromBus();
                    uint8_t data = fromMemory();
                    bool bit7 = ((data & 0x80) != 0);
                    data <<= 1;
                    if (*C)
                        data |= 0x01;
                    toMemory(*hl, data);
                    *C = bit7;
                    *Z = (data == 0);
                    getNextFromBus();
                } else {
                    bool bit7 = (registers[i] & 0x80) != 0;
                    registers[i] <<= 1;
                    if (*C)
                        registers[i] |= 0x01;
                    *C = bit7;
                    *Z = (registers[i] == 0);
                }
            } else {
                flags = 0x00000000;
                //RLC
                if (i == 0x07) {
                    getNextFromBus();
                    uint8_t data = fromMemory();
                    *C = ((data & 0x80) != 0);
                    data <<= 1;
                    if (*C)
                        data |= 0x01;
                    toMemory(*hl, data);
                    *Z = (data == 0);
                    getNextFromBus();
                } else {
                    *C = (registers[i] & 0x80) != 0;
                    registers[i] <<= 1;
                    if (*C)
                        registers[i] |= 0x01;
                    *Z = (registers[i] == 0);
                }
            }
        }
    }
    getNextFromBus();
}

// UNKNOWN / ERROR STATE //

void unknown() {
    error = "Unknown opcode.";
    errorOpcode = *opcode;
    running = false;
}

void (*opcodes[256])() = {
      /*  ..0       ..1       ..2       ..3       ..4       ..5       ..6       ..7         ..8       ..9       ..a       ..b       ..c       ..d       ..e       ..f */
/*0..*/    noop1, ld_r_d16, ld_mem_A,  inc_r16,    inc_b,    dec_b,  ld_b_d8,     rlca,  ld_a16_SP, add_HL_r, ld_A_mem,  dec_r16,    inc_c,    dec_c,  ld_c_d8,     rrca,
/*1..*/  unknown, ld_r_d16, ld_mem_A,  inc_r16,    inc_d,    dec_d,  ld_d_d8,      rla,      noop3, add_HL_r, ld_A_mem,  dec_r16,    inc_e,    dec_e,  ld_e_d8,      rra,
/*2..*/    jr_nz, ld_r_d16, ld_mem_A,  inc_r16,    inc_h,    dec_h,  ld_h_d8,      daa,       jr_z, add_HL_r, ld_A_mem,  dec_r16,    inc_l,    dec_l,  ld_l_d8,      cpl,
/*3..*/  noop2_3, ld_r_d16, ld_mem_A,  inc_r16,   inc_HL,   dec_HL, ld_HL_d8,      scf,    noop2_3, add_HL_r, ld_A_mem,  dec_r16,    inc_a,    dec_a,  ld_a_d8,      ccf,
/*4..*/    noop1,   ld_b_c,   ld_b_d,   ld_b_e,   ld_b_h,   ld_b_l,  ld_b_HL,   ld_b_a,     ld_c_b,    noop1,   ld_c_d,   ld_c_e,   ld_c_h,   ld_c_l,  ld_c_HL,   ld_c_a,
/*5..*/   ld_d_b,   ld_d_c,    noop1,   ld_d_e,   ld_d_h,   ld_d_l,  ld_d_HL,   ld_d_a,     ld_e_b,   ld_e_c,   ld_e_d,    noop1,   ld_e_h,   ld_e_l,  ld_e_HL,   ld_e_a,
/*6..*/   ld_h_b,   ld_h_c,   ld_h_d,   ld_h_e,    noop1,   ld_h_l,  ld_h_HL,   ld_h_a,     ld_l_b,   ld_l_c,   ld_l_d,   ld_l_e,   ld_l_h,    noop1,  ld_l_HL,   ld_l_a,
/*7..*/  ld_HL_b,  ld_HL_c,  ld_HL_d,  ld_HL_e,  ld_HL_h,  ld_HL_l,     halt,  ld_HL_a,     ld_a_b,   ld_a_c,   ld_a_d,   ld_a_e,   ld_a_h,   ld_a_l,  ld_a_HL,    noop1,
/*8..*/  add_A_b,  add_A_c,  add_A_d,  add_A_e,  add_A_h,  add_A_l, add_A_HL,  add_A_a,    adc_A_b,  adc_A_c,  adc_A_d,  adc_A_e,  adc_A_h,  adc_A_l, adc_A_HL,  adc_A_a,
/*9..*/  sub_A_b,  sub_A_c,  sub_A_d,  sub_A_e,  sub_A_h,  sub_A_l, sub_A_HL,  sub_A_a,    sbc_A_b,  sbc_A_c,  sbc_A_d,  sbc_A_e,  sbc_A_h,  sbc_A_l, sbc_A_HL,  sbc_A_a,
/*a..*/    and_b,    and_c,    and_d,    and_e,    and_h,    and_l,   and_HL,    and_a,      xor_b,    xor_c,    xor_d,    xor_e,    xor_h,    xor_l,   xor_HL,    xor_a,
/*b..*/     or_b,     or_c,     or_d,     or_e,     or_h,     or_l,    or_HL,     or_a,       cp_b,     cp_c,     cp_d,     cp_e,     cp_h,     cp_l,    cp_HL,     cp_a,
/*c..*/   ret2_5,  pop_r16,  noop3_4,    noop4,  call3_6, push_r16, add_A_d8,      rst,     ret2_5,     ret4,  noop3_4,      xCB,  call3_6,    call6, adc_A_d8,      rst,
/*d..*/   ret2_5,  pop_r16,  noop3_4,  unknown,  call3_6, push_r16, sub_A_d8,      rst,     ret2_5,    reti4,  noop3_4,  unknown,  call3_6,  unknown, sbc_A_d8,      rst,
/*e..*/  ld_a8_A,  pop_r16,  ld_aC_A,  unknown,  unknown, push_r16,   and_d8,      rst,  add_SP_s8,    noop1, ld_a16_A,  unknown,  unknown,  unknown,   xor_d8,      rst,
/*f..*/  ld_A_a8,  pop_r16,  ld_A_aC,       di,  unknown, push_r16,    or_d8,      rst, ld_HL_SPs8, ld_SP_HL, ld_A_a16,       ei,  unknown,  unknown,    cp_d8,      rst
};