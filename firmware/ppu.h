#ifndef GBINTERCEPTOR_PPU
#define GBINTERCEPTOR_PPU

#include "pico/stdlib.h"
#include "pico/sync.h"
#include "jpeg/jpeg.h"

#define SCREEN_W 160
#define SCREEN_H 144
#define SCREEN_SIZE (SCREEN_W * SCREEN_H)
#define FRAME_SIZE (JPEG_DATA_SIZE + JPEG_HEADER_SIZE + JPEG_END_SIZE) //Header, Huffman tables, quantization tables etc.

#define CYCLES_PER_FRAME 17556
#define CYCLES_PER_LINE 114
#define CYCLES_MODE_0 51 //max, actually 87 to 204 dots
#define CYCLES_MODE_1 1140
#define CYCLES_MODE_2 20
#define CYCLES_MODE_3 43 //min, actually 172 to 289 dots
#define CYCLES_LATEST_HBLANK (CYCLES_PER_LINE - 21) //At that point we are certainly in hblank
#define LINES 154

bool swapFrontbuffer();
void ppuInit();
void ppuStep(uint advance);

extern uint8_t volatile * frontBuffer;
extern uint8_t volatile * readyBuffer;
extern uint8_t volatile * backBuffer;
extern uint8_t volatile * lastBuffer;

extern bool frameBlending;

enum RenderState {done = 0, start, oamSearchDone, rendering};  
extern volatile enum RenderState renderState;

extern uint lineCycle;
extern int y;

extern bool readyBufferIsNew;
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

#endif