#ifndef GBINTERCEPTOR_GAME_DETECTION
#define GBINTERCEPTOR_GAME_DETECTION

#include "pico/stdlib.h"

extern volatile uint vramHash1, vramHash2;
extern struct GameInfo gameInfo;
extern volatile bool gameDetected;

struct GameInfo {
    uint vramHash1, vramHash2;
    uint16_t dmaFix; // Address that recognizes return after DMA (if not 0x0000)
    uint16_t reconstruct; //Try to reconstruct the content of this address from conditional jumps (if not 0x0000)
    bool useImmediateIRQ; //Use vblank IRQ to sync the PPU even if it occured immediately after enabling interrupts, so it might have been delayed.
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
