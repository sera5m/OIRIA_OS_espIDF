/*
 * D_st7789v2_s3psram.h
 *
 *  Created on: Jan 19, 2026
 *      Author: dev
 */


//#ifndef LCDRIVER_H_
//#define LCDRIVER_H_

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include <stdint.h>
#include <stdbool.h>
#include "hardware/wiring/wiring.h"

#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include "code_stuff/types.h"
/*  REQUIRED EXTERNAL DEFINES
    SCREEN_W
    SCREEN_H
    X_OFFSET
    Y_OFFSET
    CHUNK_SIZE
*/
extern uint16_t *framebuffer;
extern uint16_t fpsLimiterTarget;

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
 void spi_init_dma(void);
void lcd_init_simple(void);

// --------------------- FRAMEBUFFER ---------------------
 void framebuffer_alloc(void);
void fb_clear(uint16_t color);
 void lcd_refreshScreen(void);

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

// --------------------- TEXT ---------------------
void fb_draw_text( uint8_t angle, int x, int y, const char* str, uint16_t color, uint8_t size, 
 const uint8_t* font, uint8_t transparency, bool drawblocksforbackground, uint16_t blockBackground_color,
  uint16_t maxTLenBeforeAutoWrapToNextLine, struct fontcharsize fontSize );


#ifdef __cplusplus
}
#endif