/* ssd1351Driver.c
 * SSD1351 SPI driver matching the ST7789v2 state-machine style.
 * Key differences handled here:
 * - Window set is 0x15 / 0x75 / 0x5C (8-bit coords)
 * - Pointer must be reloaded for every transfer
 * - Lower max safe SPI speed
 * - Smaller chunk size for partial updates
 * - RGB565 GDDRAM, horizontal increment default
 */
#include "ssd1351Driver.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include "esp_task_wdt.h"

#define TAG "SSD1351"
#define SAFE_ROWS_PER_CHUNK SSD1351_SAFE_ROWS_PER_CHUNK
#define SCREEN_W SSD1351_W
#define SCREEN_H SSD1351_H

uint16_t *framebuffer = NULL;
uint16_t *framebuffer_front = NULL;
uint16_t *framebuffer_back = NULL;
bool framebuffer_allocd = false;

static uint8_t changed_rows_bitmask[(SCREEN_H + 7) / 8];
static SemaphoreHandle_t fb_mutex = NULL;

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
}

static inline void mark_all_dirty(bool dirty)
{
    memset(changed_rows_bitmask, dirty ? 0xFF : 0x00, sizeof(changed_rows_bitmask));
}

static inline void fill_row32(uint16_t* dst, int width, uint16_t color)
{
    if (width <= 0) return;
    uint32_t c32 = ((uint32_t)color << 16) | color;
    uint32_t* d32 = (uint32_t*)dst;
    int words = width / 2;
    for (int i = 0; i < words; i++) d32[i] = c32;
    if (width & 1) dst[width - 1] = color;
}

void framebuffer_alloc(void)
{
    if (framebuffer_allocd) return;
    size_t sz = SCREEN_W * SCREEN_H * sizeof(uint16_t);
    if (!fb_mutex) fb_mutex = xSemaphoreCreateMutex();
    framebuffer_front = (uint16_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    framebuffer_back  = (uint16_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!framebuffer_front || !framebuffer_back) {
        ESP_LOGE(TAG, "Framebuffer alloc failed");
        return;
    }
    memset(framebuffer_front, 0, sz);
    memset(framebuffer_back, 0, sz);
    framebuffer = framebuffer_back;
    framebuffer_allocd = true;
    ESP_LOGI(TAG, "SSD1351 double FB allocated (%zu KB)", (sz * 2) / 1024);
}

void framebuffer_swap(void)
{
    if (!fb_mutex) return;
    xSemaphoreTake(fb_mutex, portMAX_DELAY);
    uint16_t *tmp = framebuffer_front;
    framebuffer_front = framebuffer_back;
    framebuffer_back = tmp;
    framebuffer = framebuffer_back;
    xSemaphoreGive(fb_mutex);
}

bool framebuffer_delete(void)
{
    if (!framebuffer_allocd) return false;
    free(framebuffer_front);
    free(framebuffer_back);
    framebuffer_front = framebuffer_back = framebuffer = NULL;
    framebuffer_allocd = false;
    return true;
}

void fb_clear(uint16_t color)
{
    if (!framebuffer) return;
    uint32_t c32 = ((uint32_t)color << 16) | color;
    uint32_t* p = (uint32_t*)framebuffer;
    for (size_t i = 0; i < (SCREEN_W * SCREEN_H) / 2; i++) p[i] = c32;
    mark_all_dirty(true);
}

static esp_err_t lcd_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,
        .flags = SPI_TRANS_USE_TXDATA,
        .tx_data = {cmd}
    };
    gpio_set_level(LCD_DC, 0);
    return spi_device_polling_transmit(spi_lcd, &t);
}

static esp_err_t lcd_data(uint8_t data)
{
    spi_transaction_t t = {
        .length = 8,
        .flags = SPI_TRANS_USE_TXDATA,
        .tx_data = {data}
    };
    gpio_set_level(LCD_DC, 1);
    return spi_device_polling_transmit(spi_lcd, &t);
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
    t.flags |= SPI_TRANS_DMA_USE_PSRAM;
#endif
    gpio_set_level(LCD_DC, 1);
    return spi_device_polling_transmit(spi_lcd, &t);
}

static void lcd_set_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    uint8_t buf[2];
    lcd_cmd(SSD1351_CMD_SETCOLUMN);
    buf[0] = x0; buf[1] = x1;
    lcd_data_bulk(buf, 2);
    lcd_cmd(SSD1351_CMD_SETROW);
    buf[0] = y0; buf[1] = y1;
    lcd_data_bulk(buf, 2);
    lcd_cmd(SSD1351_CMD_WRITERAM);
}

void lcd_init_simple(void)
{
    gpio_set_level(LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_cmd(SSD1351_CMD_COMMANDLOCK); lcd_data(0x12);
    lcd_cmd(SSD1351_CMD_COMMANDLOCK); lcd_data(0xB1);
    lcd_cmd(SSD1351_CMD_DISPLAYOFF);

    lcd_cmd(SSD1351_CMD_CLOCKDIV);     lcd_data(0xF1);
    lcd_cmd(SSD1351_CMD_MUXRATIO);     lcd_data(0x7F);
    lcd_cmd(SSD1351_CMD_SETREMAP);     lcd_data(0x74);
    lcd_cmd(SSD1351_CMD_STARTLINE);    lcd_data(0x00);
    lcd_cmd(SSD1351_CMD_DISPLAYOFFSET);lcd_data(0x00);
    lcd_cmd(SSD1351_CMD_SETGPIO);      lcd_data(0x00);
    lcd_cmd(SSD1351_CMD_FUNCTIONSEL);  lcd_data(0x01);
    lcd_cmd(SSD1351_CMD_PRECHARGE);    lcd_data(0x32);
    lcd_cmd(SSD1351_CMD_PRECHARGELEVEL); lcd_data(0x1F);
    lcd_cmd(SSD1351_CMD_VCOMH);        lcd_data(0x05);
    lcd_cmd(SSD1351_CMD_NORMALDISPLAY);
    lcd_cmd(SSD1351_CMD_CONTRASTABC);  lcd_data(0xC8); lcd_data(0x80); lcd_data(0xC8);
    lcd_cmd(SSD1351_CMD_CONTRASTMASTER); lcd_data(0x0F);
    lcd_cmd(SSD1351_CMD_SETVSL);       lcd_data(0xA0); lcd_data(0xB5); lcd_data(0x55);
    lcd_cmd(SSD1351_CMD_PRECHARGE2);   lcd_data(0x01);
    lcd_cmd(SSD1351_CMD_DISPLAYENHANCE); lcd_data(0xA4); lcd_data(0x00); lcd_data(0x00);

    lcd_cmd(SSD1351_CMD_DISPLAYON);
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "SSD1351 init done (SPI max ~%d Hz)", SSD1351_SPI_HZ);
}

void lcd_refresh_screen(void)
{
    lcd_fb_display_framebuffer(true, false);
}

void lcd_fb_display_framebuffer(bool only_delta, bool cope_mode)
{
    (void)cope_mode;
    if (!framebuffer_front) return;

    if (xSemaphoreTake(fb_mutex, pdMS_TO_TICKS(150)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout");
        return;
    }

    uint16_t *display_buffer = framebuffer_front;
    const uint32_t row_bytes = SCREEN_W * 2;

    if (!only_delta) {
        for (int y = 0; y < SCREEN_H; y += SAFE_ROWS_PER_CHUNK) {
            int rows = SAFE_ROWS_PER_CHUNK;
            if (y + rows > SCREEN_H) rows = SCREEN_H - y;
            lcd_set_window(0, (uint8_t)y, SCREEN_W - 1, (uint8_t)(y + rows - 1));
            lcd_data_bulk(&display_buffer[y * SCREEN_W], rows * row_bytes);
            if ((y % (SAFE_ROWS_PER_CHUNK * 4)) == 0) {
                xSemaphoreGive(fb_mutex);
                vTaskDelay(pdMS_TO_TICKS(1));
                esp_task_wdt_reset();
                xSemaphoreTake(fb_mutex, portMAX_DELAY);
            }
        }
        mark_all_dirty(false);
    } else {
        int y = 0;
        while (y < SCREEN_H) {
            if (!(changed_rows_bitmask[y/8] & (1u << (y%8)))) {
                y++;
                continue;
            }
            int y_start = y;
            while (y < SCREEN_H &&
                   (changed_rows_bitmask[y/8] & (1u << (y%8))) &&
                   (y - y_start < SAFE_ROWS_PER_CHUNK)) {
                y++;
            }
            int rows = y - y_start;
            lcd_set_window(0, (uint8_t)y_start, SCREEN_W - 1, (uint8_t)(y_start + rows - 1));
            lcd_data_bulk(&display_buffer[y_start * SCREEN_W], rows * row_bytes);
            for (int i = y_start; i < y; i++)
                changed_rows_bitmask[i/8] &= ~(1u << (i%8));
            if (rows > 2) {
                vTaskDelay(pdMS_TO_TICKS(1));
                esp_task_wdt_reset();
            }
        }
    }
    xSemaphoreGive(fb_mutex);
}

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
    if (isfilled) {
        int fx = x + borderThickness, fy = y + borderThickness;
        int fw = w - borderThickness * 2, fh = h - borderThickness * 2;
        if (fw > 0 && fh > 0) {
            for (int py = fy; py < fy + fh; py++)
                fill_row32(&framebuffer[py * SCREEN_W + fx], fw, color);
        }
    }
    mark_rows_dirty(y, y + h - 1);
}

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

void fb_draw_bitmap(int dst_x, int dst_y, int w, int h, const uint16_t* bitmap)
{
    if (!framebuffer || !bitmap) return;
    int start_x = dst_x < 0 ? 0 : dst_x;
    int start_y = dst_y < 0 ? 0 : dst_y;
    int end_x = (dst_x + w > SCREEN_W) ? SCREEN_W : dst_x + w;
    int end_y = (dst_y + h > SCREEN_H) ? SCREEN_H : dst_y + h;
    if (start_x >= end_x || start_y >= end_y) return;
    for (int y = start_y; y < end_y; y++) {
        int sy = y - dst_y;
        if (sy < 0 || sy >= h) continue;
        uint16_t* dst = &framebuffer[y * SCREEN_W + start_x];
        const uint16_t* src = &bitmap[sy * w + (start_x - dst_x)];
        memcpy(dst, src, (end_x - start_x) * sizeof(uint16_t));
    }
    mark_rows_dirty(start_y, end_y - 1);
}
