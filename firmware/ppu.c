#include "ppu.h"

#include "cpubus.h"
#include "debug.h"
//A bit excessive, but we have enough memory and can save on cycles by just letting the uint8 index overflow to achieve a ring buffer (at least I think that this would be faster)
#include "hardware/interp.h"
#include <stdio.h>

#include "font/font8x8_basic.h"

uint8_t buffer1[FRAME_SIZE];
uint8_t buffer2[FRAME_SIZE];
uint8_t buffer3[SCREEN_SIZE];
uint8_t volatile * frontBuffer = buffer1; //Sending via USB
uint8_t volatile * readyBuffer = buffer2; //Ready to start USB transfer even if we are not currently ready to swap the backBuffer
uint8_t volatile * backBuffer = buffer3;  //We render into this one
uint8_t * backBufferLine = buffer3;
bool readyBufferIsNew = false;
uint32_t * readyIterator = NULL;
uint32_t * backIterator = NULL;
uint32_t * backEnd = NULL;

bool dmgColorMode = false;
uint frameBlending = 1;
uint8_t contrastFactor;

//On-screen display (text) state
struct OnScreenDisplayText modeInfo; //Info when mode button is pressed
uint modeInfoTimeLeft = 0;
#define MODE_INFO_DURATION 50; //Duration of the mode info in frames
struct OnScreenDisplayText gameDetectedInfo; //Info when the game has been detected
uint gameDetectedInfoTimeLeft = 0;
#define GAME_DETECTED_INFO_DURATION 100; //Duration of the mode info in frames

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

void setBufferUVColors() {
    if (dmgColorMode) {
        contrastFactor = DMG_CONTRAST_FACTOR;
        for (int i = SCREEN_SIZE; i < FRAME_SIZE; i+=2) {
            buffer1[i] = DMG_COLOR_U;
            buffer2[i] = DMG_COLOR_U;
            buffer1[i+1] = DMG_COLOR_V;
            buffer2[i+1] = DMG_COLOR_V;
        }
    } else {
        contrastFactor = CONTRAST_FACTOR;
        for (int i = SCREEN_SIZE; i < FRAME_SIZE; i+=2) {
            buffer1[i] = COLOR_U;
            buffer2[i] = COLOR_U;
            buffer1[i+1] = COLOR_V;
            buffer2[i+1] = COLOR_V;
        }
    }
}

void ppuInit() {
    modeInfoTimeLeft = 0;
    modeInfo.x = 10;
    modeInfo.y = 10;
    modeInfo.width = 0;

    gameDetectedInfoTimeLeft = 0;
    gameDetectedInfo.x = 3;
    gameDetectedInfo.y = 132;
    gameDetectedInfo.width = 0;

    //Fill UV part of NV12 encoding with gray
    setBufferUVColors();

    prepareInterpolatorConfigs();
    interp_set_config(interp0, 0, &cfgMasked0);
    interp_set_config(interp0, 1, &cfgUnmasked0);
    interp_set_config(interp1, 0, &cfgMasked1);
    interp_set_config(interp1, 1, &cfgUnmasked1);
}

void showGameDetectedInfo(const char * title) {
    gameDetectedInfoTimeLeft = GAME_DETECTED_INFO_DURATION;
    gameDetectedInfo.text = title;
}

void switchRenderMode() {
    frameBlending += 1;
    if (frameBlending > 2) {
        frameBlending = 0;
        dmgColorMode = !dmgColorMode;
    }
    switch (frameBlending) {
        case 0: modeInfo.text = "Blending OFF";
                break;
        case 1: modeInfo.text = "Blending LOW";
                break;
        case 2: modeInfo.text = "Blending HIGH";
                break;
    }
    modeInfoTimeLeft = MODE_INFO_DURATION;
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
            backBufferLine[xi] = paletteBG[pixelSourceOnLine[xi]]; //For now just the palette index as it will remain relevant when drawing sprites
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

void inline renderOSDCharacter(char i, uint x, uint y, volatile uint8_t * targetBuffer, uint8_t fgColor, uint8_t bgColor) {
    for (uint yi = 0; yi < 8; yi++) {
        char line = font8x8_basic[i][yi];
        uint8_t volatile * bufferindex = targetBuffer + (y + yi) * SCREEN_W + x;
        uint8_t mask = 0x01;
        for (uint xi = 0; xi < 8; xi++) {
            if (line & mask)
                *bufferindex = fgColor;
            else
                *bufferindex = bgColor;
            mask <<= 1;
            bufferindex++;
        }
    }
}

void renderOSDFillLine(uint fromX, uint toX, uint y0, volatile uint8_t * targetBuffer, uint8_t color) {
    uint8_t volatile * bufferindex = targetBuffer + y0 * SCREEN_W + fromX;
    for (uint y = y0; y < y0 + 8; y++) {
        for (uint x = fromX; x < toX; x++) {
            *bufferindex = color;
            bufferindex++;
        }
        bufferindex += SCREEN_W - toX + fromX;
    }
}

void renderOSD(struct OnScreenDisplayText osd, volatile uint8_t * targetBuffer, uint8_t fgColor, uint8_t bgColor) {
    const char *i = osd.text;
    uint x = osd.x+1;
    uint y = osd.y+1;
    uint maxx = x + osd.width * 8;
    while (*i != '\0') {
        if (*i == '\n') {
            if (x < maxx)
                renderOSDFillLine(x, maxx, y, targetBuffer, bgColor);
            x = osd.x;
            y += 8;
        } else {
            renderOSDCharacter(*i, x, y, targetBuffer, fgColor, bgColor);
            x += 8;
        }
        if (x > maxx)
            maxx = x;
        *i++;
    }
    if (x < maxx)
        renderOSDFillLine(x, maxx, y, targetBuffer, bgColor);

    //Padding
    uint8_t volatile * topborder = targetBuffer + (osd.y) * SCREEN_W + osd.x+1;
    uint8_t volatile * bottomborder = targetBuffer + (y + 8) * SCREEN_W + osd.x+1;
    uint8_t volatile * leftborder = targetBuffer + (osd.y+1) * SCREEN_W + osd.x;
    uint8_t volatile * rightborder = targetBuffer + (osd.y+1) * SCREEN_W + maxx;
    for (uint i = osd.x; i < maxx - 1; i++) {
        *topborder = bgColor;
        *bottomborder = bgColor;
        topborder++;
        bottomborder++;
    }
    for (uint i = osd.y; i < y+7; i++) {
        *leftborder = bgColor;
        *rightborder = bgColor;
        leftborder += SCREEN_W;
        rightborder += SCREEN_W;
    }
}

void inline startBackbufferBlend() {
    if (modeInfoTimeLeft > 0) {
        renderOSD(modeInfo, backBuffer, 0xff, 0x00);
        modeInfoTimeLeft--;
    }
    if (gameDetectedInfoTimeLeft > 0) {
        renderOSD(gameDetectedInfo, backBuffer, 0x00, 0xff);
        gameDetectedInfoTimeLeft--;
    }
    readyIterator = (uint32_t *) readyBuffer;
    backIterator = (uint32_t *) backBuffer;
    backEnd = backIterator + SCREEN_SIZE / 4; //faster 32bit steps    
}

void inline continueBackbufferBlend() {
    if (backIterator < backEnd) {
        switch (frameBlending) {
            case 0:
                for (int i = 0; i < 16; i++) {
                    *readyIterator = *backIterator; //Not really efficient, but frame blending is the default as we only show slightly less than 30fps and the option to disable frame blending is mostly here for comparison
                    readyIterator++;
                    backIterator++;
                }
                break;
            case 1:
                for (int i = 0; i < 16; i++) {
                    *readyIterator = ((*readyIterator & 0xfcfcfcfc) >> 2) + 3*((*backIterator & 0xfcfcfcfc) >> 2);
                    readyIterator++;
                    backIterator++;
                }
                break;
            case 2:
                for (int i = 0; i < 16; i++) {
                    *readyIterator = ((*readyIterator & 0xfcfcfcfc) >> 1) + ((*backIterator & 0xfcfcfcfc) >> 1);
                    readyIterator++;
                    backIterator++;
                }
                break;
        }
        if (backIterator == backEnd)
            readyBufferIsNew = true;
    }
}

void inline swapFrontbuffer() {
    if (readyBufferIsNew) {
        volatile uint8_t * temp = readyBuffer;
        readyBuffer = frontBuffer;
        frontBuffer = temp;
        readyBufferIsNew = false; //We need to track this because the PPU can be halted while the USB Video then needs to keep sending the latest frame instead of switching between two buffers.
    }
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
            if (windowEnable)
                wy++;
            if (y >= LINES) {
                y = 0;
                wy = 0;
                DEBUG_MARK_YRESET
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
        }
    }
}
