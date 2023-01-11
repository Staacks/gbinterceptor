#ifndef GBINTERCEPTOR_PPU
#define GBINTERCEPTOR_PPU

#include "pico/stdlib.h"
#include "pico/sync.h"

//Default contrast and colors
#define CONTRAST_FACTOR 0x55 //Maximum: 0x55 (4bit = 0..3, 3*0x55 = 0xff)
#define COLOR_U 0x80
#define COLOR_V 0x80

//Colors and contrast for the full DMG experience
#define DMG_CONTRAST_FACTOR 0x40
#define DMG_COLOR_U 0x70
#define DMG_COLOR_V 0x76

#define SCREEN_W 160
#define SCREEN_H 144
#define SCREEN_SIZE (SCREEN_W * SCREEN_H)
#define FRAME_SIZE (SCREEN_SIZE * 3 / 2) //NV12 encoding for Windows compatibility

#define CYCLES_PER_FRAME 17556
#define CYCLES_PER_LINE 114
#define CYCLES_MODE_0 51 //max, actually 87 to 204 dots
#define CYCLES_MODE_1 1140
#define CYCLES_MODE_2 20
#define CYCLES_MODE_3 43 //min, actually 172 to 289 dots
#define CYCLES_LATEST_HBLANK (CYCLES_PER_LINE - 21) //At that point we are certainly in hblank
#define LINES 154

void switchRenderMode();
void startBackbufferBlend();
void continueBackbufferBlend();
void swapFrontbuffer();
void setBufferUVColors();
void ppuInit();
void ppuStep(uint advance);

extern uint8_t volatile * frontBuffer;
extern uint8_t volatile * backBuffer;

extern uint8_t contrastFactor;

enum RenderState {done = 0, start, oamSearchDone, rendering};  
extern volatile enum RenderState renderState;

extern uint lineCycle;
extern int y;

extern int volatile vblankOffset;

extern volatile bool windowTileMap9C00;
extern volatile bool windowEnable;
extern volatile bool tileData8000;
extern volatile bool bgTileMap9C00;
extern volatile uint objSize;
extern volatile bool objEnable;
extern volatile bool bgAndWindowDisplay;
extern volatile bool lcdAndPpuEnable;

extern volatile uint8_t paletteBG[];
extern volatile uint8_t paletteOBP0[];
extern volatile uint8_t paletteOBP1[];

struct __attribute__((__packed__)) SpriteAttribute {
	uint8_t y;
	uint8_t x;
	uint8_t tileIndex;
	uint8_t attributes;
};

struct OnScreenDisplayText {
	uint x, y;  //Position
	uint width; //Minimal width in characters, needs to be larger than widest line for multiline text, for single line text it may be zero to fit the actual text
	char *text; //Pointer to the text
};

#endif