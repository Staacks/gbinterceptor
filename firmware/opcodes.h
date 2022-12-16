#ifndef GBINTERCEPTOR_OPCODES
#define GBINTERCEPTOR_OPCODES

#include "pico/stdlib.h"

extern void (*opcodes[])();
void toMemory(uint16_t address, uint8_t data);

#endif