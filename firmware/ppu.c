#include "ppu.h"

#include "cpubus.h"
#include "jpeg/jpeg.h"
#include "debug.h"

#include "hardware/interp.h"

#include <stdio.h>

#include "gamedb/game_detection.h"

//The following buffer1 to buffer4 are just place holders and their meanings change as the pointers frontBuffer, readyBuffer, backBuffer and lastBuffer point to them.
//The PPU renders into the backBuffer in one byte per pixel format. When a frame has been completed, the backBuffer data is converted to JPEG and written to the readyBuffer.
//If frame blending is enabled, the backBuffer data is mixed with the lastBuffer in the same step whlie creating the JPEG data.
//When next rendering starts, backBuffer and lastBuffer are swapped, so we can keep a copy of the last frame for frame blending.
//Whenever a new USB frame is to be sent, frontBuffer and readyBuffer are swapped and the frontBuffer is sent. This way a new frame can be converted to JPEG while USB is still sending data.
uint8_t buffer1[FRAME_SIZE];
uint8_t buffer2[FRAME_SIZE];
uint8_t buffer3[SCREEN_SIZE];
uint8_t buffer4[SCREEN_SIZE];
uint8_t volatile * frontBuffer = buffer1; //Data that is currently (or just has been) transmitted via USB, complete JPEG file
uint8_t volatile * readyBuffer = buffer2; //Ready to start next USB transfer while we are still rendering to the backbuffer, complete JPEG file
uint8_t volatile * backBuffer = buffer3;  //We render into this one
uint8_t volatile * lastBuffer = buffer4;  //Copy of last back buffer for frame blending

uint8_t * backBufferLine = buffer3;
bool readyBufferIsNew = false;

bool frameBlending = true;

uint lineCycle = 0;
int x = 0;  //LX
int y = 0;  //LY
int wy = 0; //Window LY
volatile enum RenderState renderState;
bool inWindowRange;
int volatile vblankOffset = 0; //Set a non zero value if synchronization needs to be adjusted

bool volatile windowTileMap9C00;
bool volatile windowEnable;
bool volatile tileData8000;
bool volatile bgTileMap9C00;
uint volatile objSize;
bool volatile objEnable;
bool volatile bgAndWindowDisplay;
bool volatile lcdAndPpuEnable;

uint8_t volatile paletteBG[4];
uint8_t volatile paletteOBP0[4];
uint8_t volatile paletteOBP1[4];

uint8_t scx;
uint8_t pixelSourceOnLine[SCREEN_W]; //Tracks the source of the current color. Usually the index of the background palette, but can also be set to PIXEL_IS_SPRITE if the pixel was drawn by a sprite.
#define PIXEL_IS_SPRITE 0xff

#define SPRITES_IN_MEMORY 40
#define MAX_SPRITES_ON_LINE 10
uint nSpritesOnLine; //Number of sprites found on the current scanline
uint scanIndex;      //index of sprite in memory while scanning for sprites on scanline
struct SpriteAttribute * spritesOnLine[MAX_SPRITES_ON_LINE]; //The up to ten sprites on the current scanline
uint currentSpriteOnLine; //Index of the first sprite that has not yet been passed on the current scanline

//Different interpolator configs that we need to switch between
interp_config cfgMasked0, cfgUnmasked0, cfgMasked1, cfgUnmasked1;

void prepareInterpolatorConfigs() {
    //Setup interpolators to shift and mask bits from tile data

    cfgMasked0 = interp_default_config();
    interp_config_set_mask(&cfgMasked0, 0, 0);
    interp_config_set_cross_input(&cfgMasked0, true);

    cfgUnmasked0 = interp_default_config();
    interp_config_set_shift(&cfgUnmasked0, 1);

    cfgMasked1 = interp_default_config();
    interp_config_set_mask(&cfgMasked1, 1, 1);
    interp_config_set_cross_input(&cfgMasked1, true);

    cfgUnmasked1 = interp_default_config();
    interp_config_set_shift(&cfgUnmasked1, 1);
}

void ppuInit() {
    prepareInterpolatorConfigs();
    interp_set_config(interp0, 0, &cfgMasked0);
    interp_set_config(interp0, 1, &cfgUnmasked0);
    interp_set_config(interp1, 0, &cfgMasked1);
    interp_set_config(interp1, 1, &cfgUnmasked1);

    readyBufferIsNew = false;
    renderState = start;
    y = 0;
    x = 0;
    lineCycle = 0;
}

void renderBGTiles() {
    const uint8_t bgX = scx + x;
    const uint8_t bgY = memory[0xff42] + y;
    const uint8_t tileIndex = memory[(bgTileMap9C00 ? 0x9c00 : 0x9800) | (((uint16_t)bgY & 0x00f8) << 2) | (bgX >> 3)];
    const uint16_t tileAddress = (0x8000 | (tileIndex << 4) | (tileData8000 || tileIndex > 0x7f ? 0x0000 : 0x1000)) + ((bgY << 1) & 0x0f);
    const uint16_t lowerTileData = memory[tileAddress];
    const uint16_t upperTileData = memory[tileAddress+1] << 1;

    interp0_hw->accum[1] = lowerTileData;
    interp1_hw->accum[1] = upperTileData;

    for (int xi = x + 7; xi >= x && xi >= 0; xi--, interp0_hw->pop[1], interp1_hw->pop[1]) {
        if (xi < SCREEN_W) {
            pixelSourceOnLine[xi] = interp0_hw->peek[0] | interp1_hw->peek[0];
            backBufferLine[xi] = paletteBG[pixelSourceOnLine[xi]];
        }
    }
}

void renderWindowTiles() {
    const uint8_t windowX = x + 7 - memory[0xff4B];
    const uint8_t windowY = wy - memory[0xff4A];
    const uint8_t tileIndex = memory[(windowTileMap9C00 ? 0x9c00 : 0x9800) | (((uint16_t)windowY & 0x00f8) << 2) | (windowX >> 3)];
    const uint16_t tileAddress = (0x8000 | (tileIndex << 4) | (tileData8000 || tileIndex > 0x7f ? 0x0000 : 0x1000)) + ((windowY << 1) & 0x0f);
    const uint16_t lowerTileData = memory[tileAddress];
    const uint16_t upperTileData = memory[tileAddress+1] << 1;

    interp0_hw->accum[1] = lowerTileData;
    interp1_hw->accum[1] = upperTileData;

    for (int xi = x + 7; xi >= x && xi >= 0; xi--, interp0_hw->pop[1], interp1_hw->pop[1]) {
        if (xi < SCREEN_W) {
            pixelSourceOnLine[xi] = interp0_hw->peek[0] | interp1_hw->peek[0];
            backBufferLine[xi] = paletteBG[pixelSourceOnLine[xi]]; //For now just the palette index as it will remain relevant when drawing sprites
        }
    }
}

void renderSprites() {
    while (currentSpriteOnLine < nSpritesOnLine) {
        struct SpriteAttribute* sprite = spritesOnLine[currentSpriteOnLine];
        
        if (x + 8 < SCREEN_W && sprite->x > x + 8) //If this sprite begins after x its end will reach into the part where no BG tiles have been drawn until now, so we do not have to consider it or the ones after this for now. Exception: If we reached the end of the line, we also need to draw the ones that reach beyond the end of the line.
            break;

        uint8_t yOffset;
        uint16_t baseAddress;
        if ((sprite->attributes & 0x40) != 0) //Vertical flip
            yOffset = (y + 16 - sprite->y) ^ (objSize-1);
        else
            yOffset = (y + 16 - sprite->y);
        if (objSize == 16)
            baseAddress = ((uint16_t)((sprite->tileIndex & 0xfe) | (yOffset >= 8 ? 0x01 : 0x00)) << 4);
        else
            baseAddress = ((uint16_t)sprite->tileIndex << 4);
        
        const uint16_t spriteTileAddress = (0x8000 | baseAddress | ((yOffset & 0x07) << 1));

        interp0_hw->accum[1] = memory[spriteTileAddress];
        interp1_hw->accum[1] = memory[spriteTileAddress+1] << 1;

        uint16_t lowerTileData, upperTileData;
        if (sprite->attributes & 0x20) { //Horizontal flip
            for (int xi = sprite->x - 8; xi < sprite->x && xi < SCREEN_W; xi++, interp0_hw->pop[1], interp1_hw->pop[1]) {
                if (xi < 0 || pixelSourceOnLine[xi] == PIXEL_IS_SPRITE) //Already set by previous sprite
                    continue;
            
                uint8_t spritePixel = interp0_hw->peek[0] | interp1_hw->peek[0];
                if (spritePixel != 0) { // We have our pixel. Fetch the color and break the loop
                    if (!(sprite->attributes & 0x80 && pixelSourceOnLine[xi] != 0)) { // Not: Flag for BG1-3 priority set and background is in fact index 1-3
                        backBufferLine[xi] = (sprite->attributes & 0x10) ? paletteOBP1[spritePixel] : paletteOBP0[spritePixel];
                    }
                    pixelSourceOnLine[xi] = 0xff; //Mark as pixel found
                }  // Else: Transparent pixel, try again for the next sprite or don't draw anything
            }
        } else {
            for (int xi = sprite->x - 1; xi >= sprite->x-8 && xi >= 0; xi--, interp0_hw->pop[1], interp1_hw->pop[1]) {
                if (xi >= SCREEN_W || pixelSourceOnLine[xi] == PIXEL_IS_SPRITE) //Already set by previous sprite
                    continue;
            
                uint8_t spritePixel = interp0_hw->peek[0] | interp1_hw->peek[0];
                if (spritePixel != 0) { // We have our pixel. Fetch the color and break the loop
                    if (!(sprite->attributes & 0x80 && pixelSourceOnLine[xi] != 0)) { // Not: Flag for BG1-3 priority set and background is in fact index 1-3
                        backBufferLine[xi] = (sprite->attributes & 0x10) ? paletteOBP1[spritePixel] : paletteOBP0[spritePixel];
                    }
                    pixelSourceOnLine[xi] = 0xff; //Mark as pixel found
                }  // Else: Transparent pixel, try again for the next sprite or don't draw anything
            }
        }
        currentSpriteOnLine++;
    }
}

void renderStep() { //Renders eight pixels at once
    if (x == 0) { //We want to align our step to the grid of the background tiles within the current viewport
        scx = memory[0xff43];
        x -= (scx & 0x07);
    }

    if (bgAndWindowDisplay) {
        if (!inWindowRange)
            renderBGTiles();
        if (!inWindowRange && windowEnable && y >= memory[0xff4a] && x + 15 > memory[0xff4b]) {
            inWindowRange = true;
            x = memory[0xff4b] - 7;
        }
        if (inWindowRange)
            renderWindowTiles();
    }
    
    if (objEnable)
        renderSprites();
    x += 8;
}

void inline insertSpriteOnLine(struct SpriteAttribute * sprite) {
    int i;
    for (i = nSpritesOnLine; i > 0 && spritesOnLine[i-1]->x > sprite->x; i--)
        spritesOnLine[i] = spritesOnLine[i-1];
    spritesOnLine[i] = sprite;
    nSpritesOnLine++;
}

void oamSearch() {
    struct SpriteAttribute * sprite;
    while (nSpritesOnLine < MAX_SPRITES_ON_LINE && scanIndex < SPRITES_IN_MEMORY) {
        sprite = (struct SpriteAttribute*)&memory[0xfe00 + scanIndex * sizeof(struct SpriteAttribute)];
        if (sprite->y + objSize > y + 16 && sprite->y <= y + 16)
            insertSpriteOnLine(sprite);
        scanIndex++;
    }
}

bool inline swapFrontbuffer() {
    if (readyBufferIsNew) {
        volatile uint8_t * temp = readyBuffer;
        readyBuffer = frontBuffer;
        frontBuffer = temp;
        readyBufferIsNew = false; //We need to track this because the PPU can be halted while the USB Video then needs to keep sending the latest frame instead of switching between two buffers.
        return true;
    }
    return false;
}

void inline swapBackbuffer() {
    volatile uint8_t * temp = backBuffer;
    backBuffer = lastBuffer;
    lastBuffer = temp;
}

void ppuStep(uint advance) { //Note that due to USB interrupts on this core we might skip a few cycles and still need to keep in sync with the Game Boy
    if (!lcdAndPpuEnable)
        return;

    lineCycle += advance;

    if (renderState == rendering) {
        if (x >= SCREEN_W) {
            renderState = done;
            DEBUG_MARK_RENDERSTOP
        } else
            renderStep();
    } else {
        if (lineCycle >= CYCLES_PER_LINE) {
            lineCycle -= CYCLES_PER_LINE;
            x = 0;
            nSpritesOnLine = 0;
            scanIndex = 0;
            inWindowRange = false;
            y++;
            if (y >= LINES) {
                y = 0;
                wy = gameInfo.windowLineAlwaysPauses ? 0 : -1;
                DEBUG_MARK_YRESET
                swapBackbuffer();
                if (!readyBufferIsNew)
                    startBackbufferToJPEG(true);
            }
            if (windowEnable) {
                //I could not find solid information about this, but comparing the behaviour of the banners in Samurai Shodown
                //and the credit banners in Prehistoric Man it seems that turning the window on midframe has the same effect as
                //if it had been all along (i.e. its internal counter equals LY). Only if it is turned off midframe and then
                //turned on again its internal line counter falls behind LY by the number of lines it has been turned off.
                if (wy < 0)
                    wy = y;
                else
                    wy++;
            }
            renderState = (y >= SCREEN_H) ? done : start;
        }

        switch (renderState) {
            case start:
                DEBUG_MARK_OAMSEARCHSTART
                oamSearch();
                renderState = oamSearchDone;
                break;
            case oamSearchDone:
                DEBUG_MARK_OAMSEARCHSTOP
                if (lineCycle >= CYCLES_MODE_2) {
                    backBufferLine = (uint8_t *)backBuffer + y * SCREEN_W;
                    renderState = rendering;
                    currentSpriteOnLine = 0;
                    DEBUG_MARK_RENDERSTART
                }
                break;
            case done:
                continueBackbufferToJPEG();
                break;
        }
    }
}
