#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ST7789 wants the high byte of RGB565 first on the wire.
// ESP32 is little-endian, so a raw uint16_t comes out swapped.
// Set this true for panels that need the swap (ST7789 default).
// Other drivers (SSD1351, etc.) can turn it off.
extern bool lcd_byteswap;

// One instruction on Xtensa. Call this on any host-order color
// before it is written into the framebuffer.
static inline uint16_t lcd_color(uint16_t c)
{
    return lcd_byteswap ? __builtin_bswap16(c) : c;
}

void lcd_set_byteswap(bool on);

// Bulk invert a buffer that is already in the wrong order.
// Uses the same single-instruction swap. n is the pixel count.
void lcd_swap_buffer(uint16_t* buf, size_t n);

#ifdef __cplusplus
}
#endif
