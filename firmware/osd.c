#include "osd.h"
#include "ppu.h"
#include <stdio.h>

#include "font/font8x8_basic.h"

uint8_t osdBuffer[OSD_HEIGHT * SCREEN_W];

uint osdPosition = SCREEN_H;
uint timeRemaining = 0;

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


void animateOSD() {
    if (timeRemaining) {
        timeRemaining--;
        if (timeRemaining < OSD_HEIGHT)
            osdPosition = SCREEN_H - timeRemaining;
        else if (osdPosition > SCREEN_H - OSD_HEIGHT)
            osdPosition--;
    }
}

uint renderText(const char * text, uint8_t fgColor, uint8_t bgColor, uint8_t * buffer, uint x, uint y) {
    const char *i = text;
    uint w = 0;
    uint xi = x;
    while (*i != '\0') {
        if (*i == '\n' || w >= 19) {
            y += 9;
            xi = x;
            w = 0;
            if (*i == '\n')
                i++;
        } else {
            renderOSDCharacter(*i, xi, y, buffer, fgColor, bgColor);
            xi += 8;
            w++;
            i++;
        }
    }
    return xi;
}

void renderOSD(const char * text, uint8_t fgColor, uint8_t bgColor, uint duration) {
    //Padding
    uint8_t volatile * topborder = osdBuffer;
    for (uint i = 0; i < SCREEN_W; i++) {
        *topborder = bgColor;
        topborder++;
    }

    //Text line
    uint x = 1;
    uint y = 1;
    renderOSDFillLine(0, x, y, osdBuffer, bgColor);
    x = renderText(text, fgColor, bgColor, osdBuffer, x, y);
    renderOSDFillLine(x, SCREEN_W, y, osdBuffer, bgColor);

    timeRemaining = duration;
    osdPosition = SCREEN_H;
}