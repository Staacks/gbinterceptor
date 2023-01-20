#ifndef GBINTERCEPTOR_GAME_DETECTION
#define GBINTERCEPTOR_GAME_DETECTION

#include "pico/stdlib.h"

extern volatile uint vramHash1, vramHash2;
extern struct GameInfo * gameInfo;

struct GameInfo {
    uint vramHash1, vramHash2;
    const char title[19];
};

void resetHashes();
bool detectGame();

#define VRAM_HASH(ADDR, DATA) \
if (memory[ADDR] != DATA) { \
    vramHash1 += ((ADDR << 8) | DATA); \
    vramHash2 += vramHash1; \
}

#endif
