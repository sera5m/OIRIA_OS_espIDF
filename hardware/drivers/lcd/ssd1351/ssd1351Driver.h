/* ssd1351Driver.h
 * SSD1351 128x128 RGB565 SPI driver
 * Mirrors the ST7789v2 lcDriver style: double-buffered PSRAM framebuffer,
 * dirty-row tracking, chunked SPI push. Accounts for SSD1351 GDDRAM
 * differences (must re-set window on every transfer, 8-bit coords,
 * lower safe SPI speed).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "hardware/wiring/wiring.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SSD1351_W
#define SSD1351_W 128
#endif
#ifndef SSD1351_H
#define SSD1351_H 128
#endif

#ifndef SSD1351_SPI_HZ
#define SSD1351_SPI_HZ (15 * 1000000)
#endif

#ifndef SSD1351_SAFE_ROWS_PER_CHUNK
#define SSD1351_SAFE_ROWS_PER_CHUNK 4
#endif

#define SSD1351_CMD_SETCOLUMN   0x15
#define SSD1351_CMD_SETROW      0x75
#define SSD1351_CMD_WRITERAM    0x5C
#define SSD1351_CMD_SETREMAP    0xA0
#define SSD1351_CMD_STARTLINE   0xA1
#define SSD1351_CMD_DISPLAYOFFSET 0xA2
#define SSD1351_CMD_NORMALDISPLAY 0xA6
#define SSD1351_CMD_DISPLAYOFF  0xAE
#define SSD1351_CMD_DISPLAYON   0xAF
#define SSD1351_CMD_COMMANDLOCK 0xFD
#define SSD1351_CMD_CLOCKDIV    0xB3
#define SSD1351_CMD_MUXRATIO    0xCA
#define SSD1351_CMD_CONTRASTABC 0xC1
#define SSD1351_CMD_CONTRASTMASTER 0xC7
#define SSD1351_CMD_PRECHARGE   0xB1
#define SSD1351_CMD_PRECHARGELEVEL 0xBB
#define SSD1351_CMD_VCOMH       0xBE
#define SSD1351_CMD_FUNCTIONSEL 0xAB
#define SSD1351_CMD_SETGPIO     0xB5
#define SSD1351_CMD_DISPLAYENHANCE 0xB2
#define SSD1351_CMD_SETVSL      0xB4
#define SSD1351_CMD_PRECHARGE2  0xB6

extern spi_device_handle_t spi_lcd;

extern uint16_t *framebuffer;
extern uint16_t *framebuffer_front;
extern uint16_t *framebuffer_back;
extern bool framebuffer_allocd;

void framebuffer_alloc(void);
void framebuffer_swap(void);
bool framebuffer_delete(void);
void fb_clear(uint16_t color);

void lcd_init_simple(void);
void lcd_refresh_screen(void);
void lcd_fb_display_framebuffer(bool only_delta, bool cope_mode);

void fb_rect(bool isfilled, uint16_t borderThickness,
             int x, int y, int w, int h,
             uint16_t color, uint16_t secondarycolor);
void fb_line(int x0, int y0, int x1, int y1, uint16_t color);
void fb_draw_bitmap(int dst_x, int dst_y, int w, int h, const uint16_t* bitmap);

#ifdef __cplusplus
}
#endif
