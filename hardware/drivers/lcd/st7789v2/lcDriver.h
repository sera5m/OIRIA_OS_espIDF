/*
 * D_st7789v2_s3psram.h
 *
 *  Created on: Jan 19, 2026
 *      Author: dev
 */


//#ifndef LCDRIVER_H_
//#define LCDRIVER_H_
// lcDriver.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
//#include <string.h>
#include "driver/spi_master.h"
#include "hardware/wiring/wiring.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"

// Move this **before** extern "C" so C++ headers are visible
//#include "hardware/drivers/psram_std/psram_std.h"

#ifdef __cplusplus
extern "C" {
#endif

// ──────────────────────────────────────────────
// Required external defines (from your project)
// ──────────────────────────────────────────────
#ifndef SCREEN_W
#define SCREEN_W 240
#endif
#ifndef SCREEN_H
#define SCREEN_H 280
#endif
#ifndef X_OFFSET
#define X_OFFSET 0   // usually 0 for 240×320 panels centered
#endif
#ifndef Y_OFFSET
#define Y_OFFSET 20  // 320-280 = 40 → 20 top/bottom centering
#endif
#ifndef CHUNK_SIZE
#define CHUNK_SIZE (4096)  // max DMA buffer — we'll use smaller effective chunks
#endif

extern spi_device_handle_t spi_lcd;           // from main – must be SPI2_HOST for LCD
extern uint16_t *framebuffer;
// --------------------- ENUMS ---------------------
typedef enum {
    plain,
    border_only,
    plainAndBorder,
    staticky_plain,
    dots,
    triangletiling,
    diamondtiling,
    checkerboard,
    circles,
    lines,
    waves,
    concentric_layers_gradinent_ofthisshape,
    topographic_fakery,
    circut_fakery,
    honeycomb
} shapefillpattern;

typedef enum {
    none,
    strikethrough,
    underlined,
    italicized,
    bold,
    transparent,
    highlighted
} text_modifier;

// --------------------- STRUCTS ---------------------


// --------------------- INIT ---------------------
void framebuffer_alloc(void);
void fb_clear(uint16_t color);
void lcd_init_simple(void);
void lcd_init_angry(void);
void lcd_refresh_screen(void);              // renamed for clarity

// --------------------- DISPLAY ---------------------
void lcd_fb_display_framebuffer(bool OnlyRenderDelta, bool cope_mode);

// --------------------- DRAWING PRIMITIVES ---------------------
void fb_rect(
    bool isfilled,
    uint16_t borderThickness,
    int x, int y, int w, int h,
    uint16_t color,
    uint16_t secondarycolor
);

void fb_rect_border(
    bool isfilled,
    uint16_t borderThickness,
    int x, int y, int w, int h,
    uint16_t colorA,
    uint16_t colorB, uint8_t segment_len

); //special highlighted rect for window types or focused square things

void fb_line(
    int x0, int y0,
    int x1, int y1,
    uint16_t color
);

void fb_circle(
    int cx, int cy, int r,
    shapefillpattern mode,
    uint16_t fillColor,
    uint16_t borderColor
);

void fb_triangle(
    int x0, int y0,
    int x1, int y1,
    int x2, int y2,
    shapefillpattern mode,
    uint16_t fillColor,
    uint16_t borderColor
);

void fb_ngon(
    int cx, int cy, int r,
    uint8_t sides,
    shapefillpattern mode,
    uint16_t fillColor,
    uint16_t borderColor
);
//bitmaps, tiles, images
void fb_draw_bitmap(int dst_x, int dst_y, int w, int h,const uint16_t* bitmap);



// --------------------- TEXT ---------------------
void fb_draw_text( uint8_t angle, int x, int y, const char* str, uint16_t color, uint8_t size, 
  uint8_t transparency, bool drawblocksforbackground, uint16_t blockBackground_color,
  uint16_t maxTLenBeforeAutoWrapToNextLine, fontdata fdat );

 

#ifdef __cplusplus

}
#endif