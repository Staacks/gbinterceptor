#include "game_detection.h"
#include "games.h"

#include <stdio.h>

#include "debug.h"

volatile uint vramHash1, vramHash2;

struct GameInfo * gameInfo;

void resetHashes() {
    gameInfo = NULL;
    vramHash1 = 0;
    vramHash2 = 0;
}

bool detectGame() {
    #if defined(DEBUG_GAME_DETECTION)
        printf("Hashes: VRAM Hash1=0x%08x Hash2=0x%08x\n", vramHash1, vramHash2);
    #endif
    uint start = gameInfoDirectory[vramHash2 >> 24];
    uint end = gameInfoDirectory[(vramHash2 >> 24) + 1];
    while (start < end) {
        uint mid = start + (end - start) / 2;
        if (gameInfos[mid].vramHash2 == vramHash2) {
            if (gameInfos[mid].vramHash1 == vramHash1) {
                gameInfo = &gameInfos[mid];
                printf("Detected %s\n", gameInfo->title);
                return true;
            } else if (gameInfos[mid].vramHash1 < vramHash1) {
                start = mid + 1;
            } else {
                end = mid;
            }
        } else if (gameInfos[mid].vramHash2 < vramHash2) {
            start = mid + 1;
        } else {
            end = mid;
        }
    }
    return false;
}
