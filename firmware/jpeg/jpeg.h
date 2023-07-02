#ifndef GBINTERCEPTOR_JPEG
#define GBINTERCEPTOR_JPEG

#include "pico/stdlib.h"
#include "ppu.h"

#define JPEG_DATA_SIZE (SCREEN_SIZE * 5 / 8) //5bit per pixel, see https://github.com/Staacks/gbinterceptor/issues/17
#define JPEG_HEADER_SIZE 160
#define JPEG_END_SIZE 2

void prepareJpegEncoding();
void startBackbufferToJPEG(bool allowFrameBlend);
void continueBackbufferToJPEG();

#endif