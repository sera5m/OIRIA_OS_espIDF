#pragma once
#include <stdint.h>
/* 1-bit packed, row-major, MSB first. 16x16 = 32 bytes. */
void icon_draw_1bit(int x, int y, int w, int h, const uint8_t* bits, uint16_t color);
