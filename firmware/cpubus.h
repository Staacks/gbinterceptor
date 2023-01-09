#ifndef GBINTERCEPTOR_CPUBUS
#define GBINTERCEPTOR_CPUBUS

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "ppu.h"

extern uint32_t cycleRatio;

extern volatile bool running;
extern volatile const char * error;
extern bool volatile errorIsStall;
extern volatile int errorOpcode;

extern PIO busPIO;
extern uint32_t busPIOemptyMask, busPIOstallMask;
extern uint busSM;

extern uint32_t volatile rawBusData;
extern uint8_t volatile * opcode;
extern uint16_t volatile * address;
extern uint8_t volatile * extra;

#define HISTORY_READAHEAD 5
extern uint32_t history[];
extern uint volatile cycleIndex;
extern uint8_t volatile * historyIndex; //Index for history array, lowest byte of cycleIndex
extern uint volatile div;

extern uint ignoreCycles;
extern bool cartridgeDMA;
extern uint cartridgeDMAsrc;
extern uint cartridgeDMAdst;

extern mutex_t cpubusMutex;
#define DUMPMORE 10 //Additional lines to dump after error


extern volatile uint8_t memory[];

//CPU registers
extern uint8_t registers[];
extern uint8_t * b;
extern uint8_t * c;
extern uint8_t * d;
extern uint8_t * e;
extern uint8_t * h;
extern uint8_t * l;
// +6 is unused to align af with opcode order
extern uint8_t * a;
extern uint8_t * f;
extern uint16_t * bc;
extern uint16_t * de;
extern uint16_t * hl;
extern uint16_t sp;

extern uint32_t flags;
extern uint8_t *Z, *N, *H, *C;

extern bool interruptsEnabled;
extern uint interruptsEnableCycle;

void handleMemoryBus();

void getNextFromBus();

void dmaToOAM(uint16_t source);

void stop(const char* errorMsg);

#endif