#include "debug.h"

#include <stdio.h>
#include "pico/stdlib.h"

#include "cpubus.h"

#ifdef DEBUG_LOG_REGISTERS
    uint32_t volatile registerHistory32[64][2];
    uint16_t volatile spHistory[64];
    uint32_t volatile flagHistory[64];
#endif

#ifdef DEBUG_PPU_TIMING
bool recordPPUTiming = false;
bool ppuTimingReady = false;
bool recordPPUTimingStarted = false;
uint ppuTiming[LINES][4];//OAM search start, OAM search done, rendering start, rendering done
struct PPUTimingEvents ppuTimingEvents;
#endif

#ifdef DEBUG_EVENTS
const char * opcodeNames[256] = {
"NOP        ","LD BC,d16  ","LD (BC),A  ","INC BC     ","INC B      ","DEC B      ","LD B,d8    ","RLCA       ","LD (a16),SP","ADD HL,BC  ","LD A,(BC)  ","DEC BC     ","INC C      ","DEC C      ","LD C,d8    ","RRCA       ",
"STOP 0     ","LD DE,d16  ","LD (DE),A  ","INC DE     ","INC D      ","DEC D      ","LD D,d8    ","RLA        ","JR r8      ","ADD HL,DE  ","LD A,(DE)  ","DEC DE     ","INC E      ","DEC E      ","LD E,d8    ","RRA        ",
"JR NZ,r8   ","LD HL,d16  ","LD (HL+),A ","INC HL     ","INC H      ","DEC H      ","LD H,d8    ","DAA        ","JR Z,r8    ","ADD HL,HL  ","LD A,(HL+) ","DEC HL     ","INC L      ","DEC L      ","LD L,d8    ","CPL        ",
"JR NC,r8   ","LD SP,d16  ","LD (HL-),A ","INC SP     ","INC (HL)   ","DEC (HL)   ","LD (HL),d8 ","SCF        ","JR C,r8    ","ADD HL,SP  ","LD A,(HL-) ","DEC SP     ","INC A      ","DEC A      ","LD A,d8    ","CCF        ",
"LD B,B     ","LD B,C     ","LD B,D     ","LD B,E     ","LD B,H     ","LD B,L     ","LD B,(HL)  ","LD B,A     ","LD C,B     ","LD C,C     ","LD C,D     ","LD C,E     ","LD C,H     ","LD C,L     ","LD C,(HL)  ","LD C,A     ",
"LD D,B     ","LD D,C     ","LD D,D     ","LD D,E     ","LD D,H     ","LD D,L     ","LD D,(HL)  ","LD D,A     ","LD E,B     ","LD E,C     ","LD E,D     ","LD E,E     ","LD E,H     ","LD E,L     ","LD E,(HL)  ","LD E,A     ",
"LD H,B     ","LD H,C     ","LD H,D     ","LD H,E     ","LD H,H     ","LD H,L     ","LD H,(HL)  ","LD H,A     ","LD L,B     ","LD L,C     ","LD L,D     ","LD L,E     ","LD L,H     ","LD L,L     ","LD L,(HL)  ","LD L,A     ",
"LD (HL),B  ","LD (HL),C  ","LD (HL),D  ","LD (HL),E  ","LD (HL),H  ","LD (HL),L  ","HALT       ","LD (HL),A  ","LD A,B     ","LD A,C     ","LD A,D     ","LD A,E     ","LD A,H     ","LD A,L     ","LD A,(HL)  ","LD A,A     ",
"ADD A,B    ","ADD A,C    ","ADD A,D    ","ADD A,E    ","ADD A,H    ","ADD A,L    ","ADD A,(HL) ","ADD A,A    ","ADC A,B    ","ADC A,C    ","ADC A,D    ","ADC A,E    ","ADC A,H    ","ADC A,L    ","ADC A,(HL) ","ADC A,A    ",
"SUB B      ","SUB C      ","SUB D      ","SUB E      ","SUB H      ","SUB L      ","SUB (HL)   ","SUB A      ","SBC A,B    ","SBC A,C    ","SBC A,D    ","SBC A,E    ","SBC A,H    ","SBC A,L    ","SBC A,(HL) ","SBC A,A    ",
"AND B      ","AND C      ","AND D      ","AND E      ","AND H      ","AND L      ","AND (HL)   ","AND A      ","XOR B      ","XOR C      ","XOR D      ","XOR E      ","XOR H      ","XOR L      ","XOR (HL)   ","XOR A      ",
"OR B       ","OR C       ","OR D       ","OR E       ","OR H       ","OR L       ","OR (HL)    ","OR A       ","CP B       ","CP C       ","CP D       ","CP E       ","CP H       ","CP L       ","CP (HL)    ","CP A       ",
"RET NZ     ","POP BC     ","JP NZ,a16  ","JP a16     ","CALL NZ,a16","PUSH BC    ","ADD A,d8   ","RST 00H    ","RET Z      ","RET        ","JP Z,a16   ","PREFIX CB  ","CALL Z,a16 ","CALL a16   ","ADC A,d8   ","RST 08H    ",
"RET NC     ","POP DE     ","JP NC,a16  ","ERROR      ","CALL NC,a16","PUSH DE    ","SUB d8     ","RST 10H    ","RET C      ","RETI       ","JP C,a16   ","ERROR      ","CALL C,a16 ","ERROR      ","SBC A,d8   ","RST 18H    ",
"LDH (a8),A ","POP HL     ","LD (C),A   ","ERROR      ","ERROR      ","PUSH HL    ","AND d8     ","RST 20H    ","ADD SP,r8  ","JP (HL)    ","LD (a16),A ","ERROR      ","ERROR      ","ERROR      ","XOR d8     ","RST 28H    ",
"LDH A,(a8) ","POP AF     ","LD A,(C)   ","DI         ","ERROR      ","PUSH AF    ","OR d8      ","RST 30H    ","LD HL,SP+r8","LD SP,HL   ","LD A,(a16) ","EI         ","ERROR      ","ERROR      ","CP d8      ","RST 38H    "
};

const char * opcodeNames16bit[256] = {
"RLC B      ","RLC C      ","RLC D      ","RLC E      ","RLC H      ","RLC L      ","RLC (HL)   ","RLC A      ","RRC B      ","RRC C      ","RRC D      ","RRC E      ","RRC H      ","RRC L      ","RRC (HL)   ","RRC A      ",
"RL B       ","RL C       ","RL D       ","RL E       ","RL H       ","RL L       ","RL (HL)    ","RL A       ","RR B       ","RR C       ","RR D       ","RR E       ","RR H       ","RR L       ","RR (HL)    ","RR A       ",
"SLA B      ","SLA C      ","SLA D      ","SLA E      ","SLA H      ","SLA L      ","SLA (HL)   ","SLA A      ","SRA B      ","SRA C      ","SRA D      ","SRA E      ","SRA H      ","SRA L      ","SRA (HL)   ","SRA A      ",
"SWAP B     ","SWAP C     ","SWAP D     ","SWAP E     ","SWAP H     ","SWAP L     ","SWAP (HL)  ","SWAP A     ","SRL B      ","SRL C      ","SRL D      ","SRL E      ","SRL H      ","SRL L      ","SRL (HL)   ","SRL A      ",
"BIT 0,B    ","BIT 0,C    ","BIT 0,D    ","BIT 0,E    ","BIT 0,H    ","BIT 0,L    ","BIT 0,(HL) ","BIT 0,A    ","BIT 1,B    ","BIT 1,C    ","BIT 1,D    ","BIT 1,E    ","BIT 1,H    ","BIT 1,L    ","BIT 1,(HL) ","BIT 1,A    ",
"BIT 2,B    ","BIT 2,C    ","BIT 2,D    ","BIT 2,E    ","BIT 2,H    ","BIT 2,L    ","BIT 2,(HL) ","BIT 2,A    ","BIT 3,B    ","BIT 3,C    ","BIT 3,D    ","BIT 3,E    ","BIT 3,H    ","BIT 3,L    ","BIT 3,(HL) ","BIT 3,A    ",
"BIT 4,B    ","BIT 4,C    ","BIT 4,D    ","BIT 4,E    ","BIT 4,H    ","BIT 4,L    ","BIT 4,(HL) ","BIT 4,A    ","BIT 5,B    ","BIT 5,C    ","BIT 5,D    ","BIT 5,E    ","BIT 5,H    ","BIT 5,L    ","BIT 5,(HL) ","BIT 5,A    ",
"BIT 6,B    ","BIT 6,C    ","BIT 6,D    ","BIT 6,E    ","BIT 6,H    ","BIT 6,L    ","BIT 6,(HL) ","BIT 6,A    ","BIT 7,B    ","BIT 7,C    ","BIT 7,D    ","BIT 7,E    ","BIT 7,H    ","BIT 7,L    ","BIT 7,(HL) ","BIT 7,A    ",
"RES 0,B    ","RES 0,C    ","RES 0,D    ","RES 0,E    ","RES 0,H    ","RES 0,L    ","RES 0,(HL) ","RES 0,A    ","RES 1,B    ","RES 1,C    ","RES 1,D    ","RES 1,E    ","RES 1,H    ","RES 1,L    ","RES 1,(HL) ","RES 1,A    ",
"RES 2,B    ","RES 2,C    ","RES 2,D    ","RES 2,E    ","RES 2,H    ","RES 2,L    ","RES 2,(HL) ","RES 2,A    ","RES 3,B    ","RES 3,C    ","RES 3,D    ","RES 3,E    ","RES 3,H    ","RES 3,L    ","RES 3,(HL) ","RES 3,A    ",
"RES 4,B    ","RES 4,C    ","RES 4,D    ","RES 4,E    ","RES 4,H    ","RES 4,L    ","RES 4,(HL) ","RES 4,A    ","RES 5,B    ","RES 5,C    ","RES 5,D    ","RES 5,E    ","RES 5,H    ","RES 5,L    ","RES 5,(HL) ","RES 5,A    ",
"RES 6,B    ","RES 6,C    ","RES 6,D    ","RES 6,E    ","RES 6,H    ","RES 6,L    ","RES 6,(HL) ","RES 6,A    ","RES 7,B    ","RES 7,C    ","RES 7,D    ","RES 7,E    ","RES 7,H    ","RES 7,L    ","RES 7,(HL) ","RES 7,A    ",
"SET 0,B    ","SET 0,C    ","SET 0,D    ","SET 0,E    ","SET 0,H    ","SET 0,L    ","SET 0,(HL) ","SET 0,A    ","SET 1,B    ","SET 1,C    ","SET 1,D    ","SET 1,E    ","SET 1,H    ","SET 1,L    ","SET 1,(HL) ","SET 1,A    ",
"SET 2,B    ","SET 2,C    ","SET 2,D    ","SET 2,E    ","SET 2,H    ","SET 2,L    ","SET 2,(HL) ","SET 2,A    ","SET 3,B    ","SET 3,C    ","SET 3,D    ","SET 3,E    ","SET 3,H    ","SET 3,L    ","SET 3,(HL) ","SET 3,A    ",
"SET 4,B    ","SET 4,C    ","SET 4,D    ","SET 4,E    ","SET 4,H    ","SET 4,L    ","SET 4,(HL) ","SET 4,A    ","SET 5,B    ","SET 5,C    ","SET 5,D    ","SET 5,E    ","SET 5,H    ","SET 5,L    ","SET 5,(HL) ","SET 5,A    ",
"SET 6,B    ","SET 6,C    ","SET 6,D    ","SET 6,E    ","SET 6,H    ","SET 6,L    ","SET 6,(HL) ","SET 6,A    ","SET 7,B    ","SET 7,C    ","SET 7,D    ","SET 7,E    ","SET 7,H    ","SET 7,L    ","SET 7,(HL) ","SET 7,A    "
};
#endif

void dumpBus() { //Dump opcode history
    bool pastIndicator = false;
    uint8_t dumpIndicator = *historyIndex - DUMPMORE + HISTORY_READAHEAD; 
    *historyIndex += HISTORY_READAHEAD;
    printf("\n===============================\n");
    bool isPrefixOpcode = false;
    for (int i = 0; i < 256; i++) {
        (*historyIndex)++;
        const uint32_t b = history[*historyIndex];
        const char * event = "           ";
        #ifdef DEBUG_EVENTS
        if (!pastIndicator) {
            if ((b & 0x01000000) != 0) {
                isPrefixOpcode = ((uint8_t)(b >> 16) == 0xcb);
                event = opcodeNames[(uint8_t)(b >> 16)];
            } else if ((b & 0x02000000) != 0) {
                event = "IRQ        ";
            } else if (isPrefixOpcode) {
                event = opcodeNames16bit[(uint8_t)(b >> 16)];
                isPrefixOpcode = false;
            }
        }
        #endif
        printf("%s %02x %s %s %s %04x %02x %s",
            *historyIndex == dumpIndicator ? ">" : " ",
            (uint8_t)(b >> 24),
            b & 0x20000000 ? "nWR" : " WR",
            b & 0x40000000 ? "nRD" : " RD",
            b & 0x80000000 ? "nCS" : " CS",
            (uint16_t)b,
            (uint8_t)(b >> 16),
            event);
        #ifdef DEBUG_LOG_REGISTERS
            if ((b & 0x01000000) != 0 && i >= 0xc0) {
                uint8_t volatile * reg = (uint8_t volatile *)&registerHistory32[*historyIndex & 0x3f];
                uint8_t volatile * flags = (uint8_t volatile*)&flagHistory[*historyIndex & 0x3f];
                printf(" A=%02x BC=%02x%02x DE=%02x%02x HL=%02x%02x SP=%04x %s%s%s%s",
                    reg[6], reg[1], reg[0], reg[3], reg[2], reg[5], reg[4], //See registers in cpubus.c about the uninstuitive order
                    spHistory[*historyIndex & 0x3f],
                    flags[0] ? "Z" : "-", flags[1] ? "N" : "-", flags[2] ? "H" : "-", flags[3] ? "C" : "-"
                    );
            }
        #endif
        if (*historyIndex == dumpIndicator)
            pastIndicator = true;
        printf("\n");
    }
    printf("\n===============================\n\n");

    printf("Registers:\n");
    printf("A   B   C   D   E   H   L    SP    Flags\n");
    printf("%02x  %02x  %02x  %02x  %02x  %02x  %02x  %04x  %s %s %s %s\n", *a, *b, *c, *d , *e, *h, *l, sp, *Z ? "Z" : "-", *N ? "N" : "-", *H ? "H" : "-", *C ? "C" : "-");

    printf("\n===============================\n\n");

    if (error != NULL)
        printf("%s\n", error);
    if (errorOpcode >= 0)
        printf("Opcode: %02x\n", errorOpcode);
}

#ifdef DEBUG_PPU_TIMING

void printPPUTiming() {
    ppuTimingReady = false;
    printf("\n ===============================\n");
    printf("|  y  | OAM SEARCH | RENDERING  |\n");
    printf("|-----|------------|------------|\n");
    for (int line = 0; line < LINES; line++) {
        printf("|% 4d |% 4d ..% 4d |% 4d ..% 4d |\n", line, ppuTiming[line][0], ppuTiming[line][1], ppuTiming[line][2], ppuTiming[line][3]);
    }
    printf(" ===============================\n");
    printf("Frame: %d .. %d => %d (Should be %d), vblank adjusted by %d\n\n", ppuTimingEvents.frameStartCycle, ppuTimingEvents.frameEndCycle, ppuTimingEvents.frameEndCycle - ppuTimingEvents.frameStartCycle, CYCLES_PER_FRAME, ppuTimingEvents.vblankOffset);
}

#endif

void dumpMemory() {
    //Dump our copy of the memory
    bool skipping = false;
    for (int baseAddr = 0x8000; baseAddr < 0x010000; baseAddr += 0x10) {
        bool skipThis = true;
        for (int subAddr = 0x00; subAddr < 0x10; subAddr++)
            if (memory[baseAddr | subAddr] != 0)
                skipThis = false;
        if (skipThis) {
            if (!skipping) {
                skipping = true;
                printf("....\n");
            }
        } else {
            skipping = false;
            printf("%04x   ", baseAddr);
            for (int subAddr = 0x00; subAddr < 0x10; subAddr++) {
                printf("%02x ", memory[baseAddr | subAddr]);
                if (subAddr == 0x07)
                    printf("  ");
            }
            printf("\n");
        }
    }
    printf("\n");
}