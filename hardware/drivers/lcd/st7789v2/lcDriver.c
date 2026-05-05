#include "lcDriver.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>
#include "esp_timer.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>     // for min/max
#include <stdlib.h>   // for min/max if needed

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAG "ST7789"

#define SAFE_ROWS_PER_CHUNK 8

// ──────────────────────────────────────────────
// Dirty rows
// ──────────────────────────────────────────────
uint16_t *framebuffer = NULL;

#define CHANGED_ROWS_BYTES ((SCREEN_H + 7)/8)
static uint8_t changed_rows_bitmask[CHANGED_ROWS_BYTES];

static inline void mark_rows_dirty(int y0, int y1)
{
    if (y0 < 0) y0 = 0;
    if (y1 >= SCREEN_H) y1 = SCREEN_H - 1;
    if (y0 > y1) return;

    int byte0 = y0 / 8;
    int byte1 = y1 / 8;

    if (byte0 == byte1) {
        changed_rows_bitmask[byte0] |= ((0xFFu << (y0 % 8)) & (0xFFu >> (7 - (y1 % 8))));
    } else {
        changed_rows_bitmask[byte0] |= (0xFFu << (y0 % 8));
        for (int i = byte0 + 1; i < byte1; i++)
            changed_rows_bitmask[i] = 0xFF;
        changed_rows_bitmask[byte1] |= (0xFFu >> (7 - (y1 % 8)));
    }
    //  taskYIELD();
}

static inline void mark_all_dirty(bool dirty)
{
    memset(changed_rows_bitmask, dirty ? 0xFF : 0x00, sizeof(changed_rows_bitmask));
    //  taskYIELD();
}

// ──────────────────────────────────────────────
// Fast fill helper
// ──────────────────────────────────────────────
static inline void fill_row32(uint16_t* dst, int width, uint16_t color)
{
    uint32_t c32 = ((uint32_t)color << 16) | color;
    uint32_t* d32 = (uint32_t*)dst;
    int words = width / 2;
    for (int i = 0; i < words; i++)
        d32[i] = c32;
    if (width & 1)
        dst[width - 1] = color;
        //  taskYIELD();
}

// ──────────────────────────────────────────────
// Framebuffer
// ──────────────────────────────────────────────
void framebuffer_alloc(void)
{
    static bool allocated = false;
    if (allocated) return;

    size_t sz = SCREEN_W * SCREEN_H * sizeof(uint16_t);
    framebuffer = (uint16_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);

    if (!framebuffer) {
        ESP_LOGE(TAG, "Framebuffer allocation failed!");
        return;
    }

    allocated = true;
    ESP_LOGI(TAG, "Framebuffer allocated in %sRAM", 
             esp_ptr_external_ram(framebuffer) ? "PS" : "Internal");
}

void fb_clear(uint16_t color)
{
    if (!framebuffer) return;
    uint32_t c32 = ((uint32_t)color << 16) | color;
    uint32_t* p = (uint32_t*)framebuffer;
    for (size_t i = 0; i < (SCREEN_W * SCREEN_H)/2; i++)
        p[i] = c32;
        //  taskYIELD();  
    mark_all_dirty(true);
}

// ──────────────────────────────────────────────
// Low-level LCD
// ──────────────────────────────────────────────
// ──────────────────────────────────────────────
// Low-level LCD
// ──────────────────────────────────────────────
static esp_err_t lcd_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,
        .flags = SPI_TRANS_USE_TXDATA,
        .tx_data = {cmd}
    };
    gpio_set_level(LCD_DC, 0);
    esp_err_t ret = spi_device_polling_transmit(spi_lcd, &t);
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "lcd_cmd(0x%02x) failed: %s", cmd, esp_err_to_name(ret));
    return ret;
}

static esp_err_t lcd_data(uint8_t data)
{
    spi_transaction_t t = {
        .length = 8,
        .flags = SPI_TRANS_USE_TXDATA,
        .tx_data = {data}
    };
    gpio_set_level(LCD_DC, 1);
    esp_err_t ret = spi_device_polling_transmit(spi_lcd, &t);
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "lcd_data(0x%02x) failed: %s", data, esp_err_to_name(ret));
    return ret;
}

static esp_err_t lcd_data_bulk(const void* data, size_t len)
{
    if (len == 0) return ESP_OK;

    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
        .flags = 0
    };

#ifdef SPI_TRANS_DMA_USE_PSRAM
    t.flags |= SPI_TRANS_DMA_USE_PSRAM;   // Use only if available
#endif

    gpio_set_level(LCD_DC, 1);
    return spi_device_polling_transmit(spi_lcd, &t);
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    x0 += X_OFFSET; x1 += X_OFFSET;
    y0 += Y_OFFSET; y1 += Y_OFFSET;

    uint8_t buf[4];

    lcd_cmd(0x2A);  // CASET
    buf[0] = x0 >> 8; buf[1] = x0 & 0xFF;
    buf[2] = x1 >> 8; buf[3] = x1 & 0xFF;
    lcd_data_bulk(buf, 4);

    lcd_cmd(0x2B);  // RASET
    buf[0] = y0 >> 8; buf[1] = y0 & 0xFF;
    buf[2] = y1 >> 8; buf[3] = y1 & 0xFF;
    lcd_data_bulk(buf, 4);

    lcd_cmd(0x2C);  // RAMWR
}

// ──────────────────────────────────────────────
// Display
// ──────────────────────────────────────────────
void lcd_fb_display_framebuffer(bool only_delta, bool cope_mode)
{
    if (!framebuffer) return;

    const uint32_t row_bytes = SCREEN_W * 2;

    if (!only_delta) {
        // Full refresh
        for (int y = 0; y < SCREEN_H; y += SAFE_ROWS_PER_CHUNK) {
            int rows = SAFE_ROWS_PER_CHUNK;
            if (y + rows > SCREEN_H) rows = SCREEN_H - y;

            lcd_set_window(0, y, SCREEN_W - 1, y + rows - 1);
            lcd_data_bulk(&framebuffer[y * SCREEN_W], rows * row_bytes);
            //  taskYIELD();
        }
        mark_all_dirty(false);
    } else {
        // Delta refresh
        int y = 0;
        while (y < SCREEN_H) {
            if (!(changed_rows_bitmask[y/8] & (1u << (y%8)))) {
                y++; continue;
            }

            int y_start = y;
            while (y < SCREEN_H && 
                   (changed_rows_bitmask[y/8] & (1u << (y%8))) &&
                   (y - y_start < SAFE_ROWS_PER_CHUNK)) {
                y++;
            }

            int rows = y - y_start;
            lcd_set_window(0, y_start, SCREEN_W - 1, y_start + rows - 1);
            lcd_data_bulk(&framebuffer[y_start * SCREEN_W], rows * row_bytes);
            //  taskYIELD();

            // Clear dirty bits
            for (int i = y_start; i < y; i++)
                changed_rows_bitmask[i/8] &= ~(1u << (i%8));
        }
    }
}

// ──────────────────────────────────────────────
// Init
// ──────────────────────────────────────────────
#define lcd_c_adr_set 0x2A

void lcd_init_simple(void)
{
    gpio_set_level(LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_cmd(0x01);      // Software reset
    vTaskDelay(pdMS_TO_TICKS(150));

    lcd_cmd(0x11);      // Sleep out
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_cmd(0x3A);
    lcd_data(0x55);     // 16-bit color

    lcd_cmd(0x36);
    lcd_data(0x00);     // Memory data access control

    // Column address
    lcd_cmd(lcd_c_adr_set);
    lcd_data(0x00); lcd_data(0x00);
    lcd_data(0x00); lcd_data(0xEF);

    // Row address
    lcd_cmd(0x2B);
    lcd_data(0x00); lcd_data(0x00);
    lcd_data(0x01); lcd_data(0x3F);

    // Other init commands...
    lcd_cmd(0xB2); lcd_data(0x0C); lcd_data(0x0C); lcd_data(0x00);
                   lcd_data(0x33); lcd_data(0x33);

    lcd_cmd(0xB7); lcd_data(0x35);
    lcd_cmd(0xBB); lcd_data(0x1F);
    lcd_cmd(0xC0); lcd_data(0x2C);
    lcd_cmd(0xC2); lcd_data(0x01);
    lcd_cmd(0xC3); lcd_data(0x12);
    lcd_cmd(0xC4); lcd_data(0x20);
    lcd_cmd(0xC6); lcd_data(0x0F);
    lcd_cmd(0xD0); lcd_data(0xA4); lcd_data(0xA1);

    // Gamma
    lcd_cmd(0xE0); // positive gamma...
    lcd_data(0xD0); lcd_data(0x08); lcd_data(0x11); lcd_data(0x08);
    lcd_data(0x0C); lcd_data(0x15); lcd_data(0x39); lcd_data(0x33);
    lcd_data(0x50); lcd_data(0x36); lcd_data(0x13); lcd_data(0x14);
    lcd_data(0x29); lcd_data(0x2D);

    lcd_cmd(0xE1); // negative gamma...
    lcd_data(0xD0); lcd_data(0x08); lcd_data(0x10); lcd_data(0x08);
    lcd_data(0x06); lcd_data(0x06); lcd_data(0x39); lcd_data(0x44);
    lcd_data(0x51); lcd_data(0x0B); lcd_data(0x16); lcd_data(0x14);
    lcd_data(0x2F); lcd_data(0x31);

    lcd_cmd(0x21);      // Inversion on
    vTaskDelay(pdMS_TO_TICKS(10));
    lcd_cmd(0x29);      // Display on
    vTaskDelay(pdMS_TO_TICKS(120));
}

// ──────────────────────────────────────────────
// Refresh
// ──────────────────────────────────────────────
uint16_t fpsLimiterTarget = 30;

void lcd_refresh_screen(void)
{
    const uint32_t FRAME_MS = 1000 / fpsLimiterTarget;
    uint32_t frame_start = esp_log_timestamp();

    lcd_fb_display_framebuffer(true, false);

    uint32_t frame_time = esp_log_timestamp() - frame_start;
    if (frame_time < FRAME_MS) {
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS - frame_time));
    }
}

// ──────────────────────────────────────────────
// Drawing Functions
// ──────────────────────────────────────────────

void fb_rect(bool isfilled, uint16_t borderThickness,
             int x, int y, int w, int h,
             uint16_t color, uint16_t secondarycolor)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    if (borderThickness * 2 > w) borderThickness = w / 2;
    if (borderThickness * 2 > h) borderThickness = h / 2;

    // Border
    if (borderThickness > 0) {
        for (int py = y; py < y + borderThickness; py++)
            fill_row32(&framebuffer[py * SCREEN_W + x], w, secondarycolor);
        for (int py = y + h - borderThickness; py < y + h; py++)
            fill_row32(&framebuffer[py * SCREEN_W + x], w, secondarycolor);

        for (int py = y + borderThickness; py < y + h - borderThickness; py++) {
            uint16_t* row = &framebuffer[py * SCREEN_W];
            for (int i = 0; i < borderThickness; i++) {
                row[x + i] = secondarycolor;
                row[x + w - borderThickness + i] = secondarycolor;
            }
        }
    }

    // Fill
    if (isfilled) {
        int fx = x + borderThickness;
        int fy = y + borderThickness;
        int fw = w - borderThickness * 2;
        int fh = h - borderThickness * 2;
        if (fw > 0 && fh > 0) {
            for (int py = fy; py < fy + fh; py++)
                fill_row32(&framebuffer[py * SCREEN_W + fx], fw, color);
        }
    }

    mark_rows_dirty(y, y + h - 1);
}

void fb_rect_border(bool isfilled, uint16_t borderThickness,
                    int x, int y, int w, int h,
                    uint16_t colorA, uint16_t colorB, uint8_t segment_len)
{
    // TODO: Optimize this one later if needed
    // For now keep functional version but fix dirty marking
    if (segment_len < 1) segment_len = 1;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    if (borderThickness * 2 > w) borderThickness = w / 2;
    if (borderThickness * 2 > h) borderThickness = h / 2;

    int period = segment_len * 2;

    // Top & Bottom
    for (int py = y; py < y + borderThickness; py++) {
        uint16_t *dst = &framebuffer[py * SCREEN_W + x];
        for (int px = 0; px < w; px++) {
            bool toggle = ((px % period) >= segment_len);
            dst[px] = toggle ? colorB : colorA;
        }
    }

    // Left & Right
    for (int py = y + borderThickness; py < y + h - borderThickness; py++) {
        bool toggle = (((py - y) % period) >= segment_len);
        uint16_t *row = &framebuffer[py * SCREEN_W];
        for (int i = 0; i < borderThickness; i++) {
            row[x + i] = toggle ? colorB : colorA;
            row[x + w - borderThickness + i] = toggle ? colorB : colorA;
        }
    }

    // Fill
    if (isfilled) {
        int fx = x + borderThickness;
        int fy = y + borderThickness;
        int fw = w - borderThickness * 2;
        int fh = h - borderThickness * 2;
        if (fw > 0 && fh > 0) {
            for (int py = fy; py < fy + fh; py++)
                fill_row32(&framebuffer[py * SCREEN_W + fx], fw, colorA);
        }
    }

    mark_rows_dirty(y, y + h - 1);
}

// Keep the rest of your functions (fb_line, fb_circle, etc.) as they are for now, 
// or tell me if you want me to optimize more of them.

void fb_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    int miny = y0 < y1 ? y0 : y1;
    int maxy = y0 > y1 ? y0 : y1;

    while (1) {
        if (x0 >= 0 && x0 < SCREEN_W && y0 >= 0 && y0 < SCREEN_H)
            framebuffer[y0 * SCREEN_W + x0] = color;

        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }

    mark_rows_dirty(miny, maxy);
}



// ──────────────────────────────────────────────
// Missing / Incomplete Functions
// ──────────────────────────────────────────────

void fb_draw_bitmap(int dst_x, int dst_y, int w, int h, const uint16_t* bitmap)
{
    if (!framebuffer || !bitmap) return;

    int start_x = dst_x < 0 ? 0 : dst_x;
    int start_y = dst_y < 0 ? 0 : dst_y;
    int end_x   = (dst_x + w > SCREEN_W) ? SCREEN_W : dst_x + w;
    int end_y   = (dst_y + h > SCREEN_H) ? SCREEN_H : dst_y + h;

    if (start_x >= end_x || start_y >= end_y) return;

    for (int y = start_y; y < end_y; y++) {
        int sy = y - dst_y;
        if (sy < 0 || sy >= h) continue;

        uint16_t* dst = &framebuffer[y * SCREEN_W + start_x];
        const uint16_t* src = &bitmap[sy * w + (start_x - dst_x)];

        memcpy(dst, src, (end_x - start_x) * sizeof(uint16_t));
    }
    //  taskYIELD();
    mark_rows_dirty(start_y, end_y - 1);
}

// ──────────────────────────────────────────────
// Text drawing (basic but working)
// ──────────────────────────────────────────────
void fb_draw_text(uint8_t angle, int x, int y, const char* str,
                  uint16_t color, uint8_t size,
                  uint8_t transparency, bool drawblocksforbackground,
                  uint16_t blockBackground_color,
                  uint16_t maxTLenBeforeAutoWrapToNextLine,
                  fontdata fdat)
{
    if (!str || !framebuffer) return;

    int ax = 1, ay = 0;   // advance
    int ux = 0, uy = 1;   // up (glyph rows)

    switch (angle & 0x1F) {
        case 0:  ax=1; ay=0; ux=0; uy=1; break;
        case 4:  ax=0; ay=1; ux=-1; uy=0; break;
        case 8:  ax=-1; ay=0; ux=0; uy=-1; break;
        case 12: ax=0; ay=-1; ux=1; uy=0; break;
        default: ax=1; ay=0; ux=0; uy=1; break;
    }

    int cursor = 0;

    while (*str) {
        char c = *str++;
        if (c < ' ' || c > '~') {
            cursor++;
            continue;
        }

        const uint8_t* glyph = &fdat.fontRef[(c - ' ') * fdat.fcs.x];

        for (int col = 0; col < fdat.fcs.x; col++) {
            uint8_t bits = glyph[col];
            int row = 0;

            while (row < fdat.fcs.y) {
                if (!(bits & (1 << row))) {
                    row++;
                    continue;
                }

                int start = row;
                while (row < fdat.fcs.y && (bits & (1 << row)))
                    row++;

                int span_len = (row - start) * size;

                int gx = (cursor * fdat.fcs.x + col) * size;
                int gy = start * size;

                int px = x + gx * ax + gy * ux;
                int py = y + gx * ay + gy * uy;

                for (int s = 0; s < span_len; s++) {
                    for (int t = 0; t < size; t++) {
                        int sx = px + s * ux + t * ax;
                        int sy = py + s * uy + t * ay;

                        if (sx >= 0 && sx < SCREEN_W && sy >= 0 && sy < SCREEN_H) {
                            framebuffer[sy * SCREEN_W + sx] = color;
                        }
                    }
                }
            }
        }
        //  taskYIELD();
        cursor++;
    }

    // Mark dirty area (conservative)
    mark_rows_dirty(y - 10, y + (fdat.fcs.y * size) + 10);
}

// ──────────────────────────────────────────────
// Refresh function name fix (you had both versions)
// ──────────────────────────────────────────────
void lcd_refreshScreen(void)
{
    lcd_refresh_screen();   // call the one you already have
}