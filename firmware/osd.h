#ifndef OSD_H
#define OSD_H

#include "pico/stdlib.h"

#define OSD_HEIGHT 9

extern uint osdPosition;
extern uint8_t osdBuffer[];

void animateOSD();
uint renderText(const char * text, uint8_t fgColor, uint8_t bgColor, uint8_t * buffer, uint x, uint y);
void renderOSD(const char * text, uint8_t fgColor, uint8_t bgColor, uint duration);

#endif