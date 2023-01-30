#ifndef GBINTERCEPTOR_GAME_DETECTION
#define GBINTERCEPTOR_GAME_DETECTION

#include "pico/stdlib.h"

#define BRANCH_BASED_FIX_LIST_SIZE 3
#define DMA_REGISTER_MAP_SIZE 20

typedef enum {nop, set, and, or, xor, sync} FixMethod;

typedef struct {
    uint16_t jumpAddress;
    uint16_t fixTarget;
    FixMethod takenMethod;
    uint8_t takenValue;
    FixMethod notTakenMethod;
    uint8_t notTakenValue;
} BranchBasedFix;

typedef struct {
    uint vramHash1, vramHash2;
    uint16_t dmaFix; // Address that recognizes return after DMA (if not 0x0000)
    bool useImmediateIRQ; //Use vblank IRQ to sync the PPU even if it occured immediately after enabling interrupts, so it might have been delayed.
    bool disableStatSyncs; //Do not use stat register related tight loops for sync
    bool disableLySyncs; //Do not use stat register related tight loops for sync
    BranchBasedFix branchBasedFixes[BRANCH_BASED_FIX_LIST_SIZE]; //List of memory addresses of conditional jumps and how their branching behavior should set values in memory
    uint8_t writeRegistersDuringDMA[DMA_REGISTER_MAP_SIZE]; //Sequence of HRAM/IO addresses. Write the first to the second, the third to the fourth etc. during DMA
    char title[19];
} GameInfo;

extern volatile uint vramHash1, vramHash2;
extern GameInfo gameInfo;
extern volatile bool gameDetected;

void resetHashes();
bool detectGame();

#define VRAM_HASH(ADDR, DATA) \
if (memory[ADDR] != DATA) { \
    vramHash1 += ((ADDR << 8) | DATA); \
    vramHash2 += vramHash1; \
}

#endif
