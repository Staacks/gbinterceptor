#ifndef GBINTERCEPTOR_GAME_DETECTION
#define GBINTERCEPTOR_GAME_DETECTION

#include "pico/stdlib.h"

extern volatile uint vramHash1, vramHash2;
extern struct GameInfo gameInfo;
extern volatile bool gameDetected;

struct GameInfo {
    uint vramHash1, vramHash2;
    uint16_t dmaFix; // Address that recognizes return after DMA (if not 0x0000)
    char title[19];
};

void resetHashes();
bool detectGame();

#define VRAM_HASH(ADDR, DATA) \
if (memory[ADDR] != DATA) { \
    vramHash1 += ((ADDR << 8) | DATA); \
    vramHash2 += vramHash1; \
}

#endif
