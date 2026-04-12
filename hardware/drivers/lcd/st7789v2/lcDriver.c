// lcDriver.c
#include "lcDriver.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>               // ← added for sqrtf, cosf, sinf

#include <stdint.h>
#include <stdbool.h>

//#include "hardware/drivers/psram_std/psram_std.h" //my custom work for psram stdd things

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif



#define SAFE_ROWS_PER_CHUNK 4   // 240×4×2 = 1920 bytes — safe on S3
#define SAFE_CHUNK_BYTES (SCREEN_W * SAFE_ROWS_PER_CHUNK * 2)

#define TAG "ST7789"

#define SAFE_ROWS_PER_CHUNK 4   // 240×4×2 = 1920 bytes — safe on S3 DMA
#define SAFE_CHUNK_BYTES (SCREEN_W * SAFE_ROWS_PER_CHUNK * 2)

// ──────────────────────────────────────────────
// Framebuffer & dirty rows
// ──────────────────────────────────────────────
uint16_t *framebuffer = NULL;

#define CHANGED_ROWS_BITS (SCREEN_H + 7)
#define CHANGED_ROWS_BYTES ((CHANGED_ROWS_BITS + 7) / 8)
static uint8_t changed_rows_bitmask[CHANGED_ROWS_BYTES];

static inline void mark_row_dirty(uint16_t row) {
    if (row >= SCREEN_H) return;
    changed_rows_bitmask[row / 8] |= (1u << (row % 8));
}

static inline bool is_row_dirty(uint16_t row) {
    if (row >= SCREEN_H) return false;
    return (changed_rows_bitmask[row / 8] & (1u << (row % 8))) != 0;
}

static inline void mark_all_dirty(bool dirty) {
    memset(changed_rows_bitmask, dirty ? 0xFF : 0x00, sizeof(changed_rows_bitmask));
}

static void mark_rows_range_dirty(int y0, int y1) {
    y0 = (y0 < 0) ? 0 : y0;
    y1 = (y1 >= SCREEN_H) ? SCREEN_H - 1 : y1;
    for (int y = y0; y <= y1; y++) {
        mark_row_dirty(y);
    }
}
// ──────────────────────────────────────────────
// Framebuffer
// ──────────────────────────────────────────────


void framebuffer_alloc(void) {
    static bool allocated = false;
    if (allocated) return;

    size_t sz = SCREEN_W * SCREEN_H * sizeof(uint16_t);

    ESP_LOGI(TAG, "Allocating framebuffer: %u bytes (%.1f KiB)", sz, sz / 1024.0f);

    // ────────────────────────────────────────────────────────
    // This is the only line that actually needed fixing
    framebuffer = (uint16_t*) heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    // ────────────────────────────────────────────────────────

    if (!framebuffer) {
        ESP_LOGE(TAG, "Framebuffer allocation failed! (PSRAM requested)");
        allocated = false;
        return;
    }

    ESP_LOGI(TAG, "Framebuffer allocated at %p", framebuffer);
    ESP_LOGI(TAG, "Is in PSRAM? %s",
             esp_ptr_external_ram(framebuffer) ? "YES" : "NO");

    if (!esp_ptr_external_ram(framebuffer)) {
        ESP_LOGE(TAG, "!!! WARNING: Framebuffer ended up in internal SRAM !!!");
        // Optional: free it here if you want to fail loudly
        // heap_caps_free(framebuffer);
        // framebuffer = NULL;
        // return;
    }

    allocated = true;

    vTaskDelay(pdMS_TO_TICKS(10));  // keep if you feel it's needed
    // fb_clear(0x0000);           // commented as before
}



void fb_clear(uint16_t color) {
    if (!framebuffer) return;

    uint32_t c = ((uint32_t)color << 16) | color;
    uint32_t *p = (uint32_t *)framebuffer;
    size_t n = (SCREEN_W * SCREEN_H) / 2;
    for (size_t i = 0; i < n; i++) p[i] = c;

    mark_all_dirty(true);
}

// ──────────────────────────────────────────────
// Low-level LCD commands
// ──────────────────────────────────────────────
static esp_err_t lcd_cmd(uint8_t cmd) {
    spi_transaction_t t = {
        .length = 8,
        .flags = SPI_TRANS_USE_TXDATA,
        .tx_data = {cmd}
    };
    gpio_set_level(LCD_DC, 0);
    esp_err_t ret = spi_device_polling_transmit(spi_lcd, &t);
    if (ret != ESP_OK) ESP_LOGE(TAG, "lcd_cmd(0x%02x) failed: %s", cmd, esp_err_to_name(ret));
    return ret;
}

static esp_err_t lcd_data(uint8_t data) {
    spi_transaction_t t = {
        .length = 8,
        .flags = SPI_TRANS_USE_TXDATA,
        .tx_data = {data}
    };
    gpio_set_level(LCD_DC, 1);
    esp_err_t ret = spi_device_polling_transmit(spi_lcd, &t);
    if (ret != ESP_OK) ESP_LOGE(TAG, "lcd_data(0x%02x) failed: %s", data, esp_err_to_name(ret));
    return ret;
}

static esp_err_t lcd_data_bulk(const void *data, size_t len) {
    if (len == 0) return ESP_OK;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data
    };
    gpio_set_level(LCD_DC, 1);
    esp_err_t ret = spi_device_polling_transmit(spi_lcd, &t);
    if (ret != ESP_OK) ESP_LOGE(TAG, "lcd_data_bulk(%u bytes) failed: %s", (unsigned)len, esp_err_to_name(ret));
    return ret;
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t data[4];

    x0 += X_OFFSET; x1 += X_OFFSET;
    y0 += Y_OFFSET; y1 += Y_OFFSET;

    lcd_cmd(0x2A); // CASET
    data[0] = x0 >> 8; data[1] = x0 & 0xFF;
    data[2] = x1 >> 8; data[3] = x1 & 0xFF;
    lcd_data_bulk(data, 4);

    lcd_cmd(0x2B); // RASET
    data[0] = y0 >> 8; data[1] = y0 & 0xFF;
    data[2] = y1 >> 8; data[3] = y1 & 0xFF;
    lcd_data_bulk(data, 4);

    lcd_cmd(0x2C); // RAMWR
}
// ──────────────────────────────────────────────
// Safe framebuffer display (chunked)
// ──────────────────────────────────────────────

void lcd_fb_display_framebuffer(bool only_delta, bool cope_mode) {
    if(!framebuffer){
        ESP_LOGE(TAG, "FRAMEBUFFER ISN'T ALLOCATED YET DUMBFUCK");
    }else{
    ESP_LOGI(TAG, "Display FB: delta=%d cope=%d", only_delta, cope_mode);

    const uint32_t row_bytes = SCREEN_W * 2;
    const uint32_t rows_per_chunk = SAFE_ROWS_PER_CHUNK;

    if (only_delta) {
        ESP_LOGI(TAG, "Delta update");
        int y = 0;
        while (y < SCREEN_H) {
            if (!is_row_dirty(y)) { y++; continue; }

            int y_start = y;
            int y_end = y;
            while (y_end + 1 < SCREEN_H &&
                   is_row_dirty(y_end + 1) &&
                   (y_end - y_start + 1) < rows_per_chunk) {
                y_end++;
            }

            uint32_t rows = y_end - y_start + 1;
            uint32_t bytes = rows * row_bytes;

            lcd_set_window(0, y_start, SCREEN_W - 1, y_end);

            esp_err_t ret = lcd_data_bulk((uint8_t *)&framebuffer[y_start * SCREEN_W], bytes);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Delta chunk y=%d–%d failed: %s", y_start, y_end, esp_err_to_name(ret));
            }

            for (int ry = y_start; ry <= y_end; ry++) {
                changed_rows_bitmask[ry / 8] &= ~(1u << (ry % 8));
            }

            y = y_end + 1;
        }
    } else {
        ESP_LOGI(TAG, "Full update");
        for (int y = 0; y < SCREEN_H; y += rows_per_chunk) {
            int rows = rows_per_chunk;
            if (y + rows > SCREEN_H) rows = SCREEN_H - y;

            uint32_t bytes = rows * row_bytes;

            lcd_set_window(0, y, SCREEN_W - 1, y + rows - 1);

            esp_err_t ret = lcd_data_bulk((uint8_t *)&framebuffer[y * SCREEN_W], bytes);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Full chunk y=%d failed: %s", y, esp_err_to_name(ret));
            }
        }
        mark_all_dirty(false);
    }

    ESP_LOGI(TAG, "Display push complete");
}
}
// ──────────────────────────────────────────────
// Simple init (unchanged but with error checking)
// ──────────────────────────────────────────────
#define lcd_c_adr_set 0x2A

 void lcd_init_simple(void)
{
    ESP_LOGI(TAG, "readying lcd init and setting gipo");
    gpio_set_level(LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Software reset
    lcd_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(150));

    // Sleep out
    lcd_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_LOGI(TAG, "setting up color modes");

    // Color mode: 16-bit/pixel (RGB565)
    lcd_cmd(0x3A);
    lcd_data(0x55);  // 0x55 = 16 bits/pixel
    vTaskDelay(pdMS_TO_TICKS(10));

    // Memory data access control
    // 0x00 = Normal orientation, RGB order
    // 0x40 = X-Mirror
    // 0x80 = Y-Mirror  
    // 0xC0 = X-Y-Mirror
    lcd_cmd(0x36);
    lcd_data(0x00);  // Try different values if display is rotated
    ESP_LOGI(TAG, "column and row addr set and porch");
    // ---------- CRITICAL FOR 240x320 ----------
    // Set display resolution to 240x320
    lcd_cmd(lcd_c_adr_set);  // Column address set 
    lcd_data(0x00); lcd_data(0x00);      // Start column = 0
    lcd_data(0x00); lcd_data(0xEF);      // End column = 239 (0xEF = 239)
    
    lcd_cmd(0x2B);  // Row address set  
    lcd_data(0x00); lcd_data(0x00);      // Start row = 0
    lcd_data(0x01); lcd_data(0x3F);      // End row = 319 (0x13F = 319)
    
    // Porch settings for 240x320
    lcd_cmd(0xB2);  // Porch control
    lcd_data(0x0C); lcd_data(0x0C); lcd_data(0x00);
    lcd_data(0x33); lcd_data(0x33);
    
    // Gate control
    lcd_cmd(0xB7);
    lcd_data(0x35);  // VGH=14.22V, VGL=-7.14V
    
    // VCOM setting
    lcd_cmd(0xBB);
    lcd_data(0x1F);  // VCOM=1.775V
    
    // LCM control
    lcd_cmd(0xC0);
    lcd_data(0x2C);
    
    // VDV and VRH command enable
    lcd_cmd(0xC2);
    lcd_data(0x01);
    
    // VRH set
    lcd_cmd(0xC3);
    lcd_data(0x12);  // VRH=4.45+VCOM
    
    // VDV set
    lcd_cmd(0xC4);
    lcd_data(0x20);  // VDV=0V
    
    // Frame rate control
    lcd_cmd(0xC6);
    lcd_data(0x0F);  // 60Hz
    
    // Power control 1
    lcd_cmd(0xD0);
    lcd_data(0xA4); lcd_data(0xA1);
    
    // Positive gamma correction
    lcd_cmd(0xE0);
    lcd_data(0xD0); lcd_data(0x08); lcd_data(0x11); lcd_data(0x08);
    lcd_data(0x0C); lcd_data(0x15); lcd_data(0x39); lcd_data(0x33);
    lcd_data(0x50); lcd_data(0x36); lcd_data(0x13); lcd_data(0x14);
    lcd_data(0x29); lcd_data(0x2D);
    
    // Negative gamma correction
    lcd_cmd(0xE1);
    lcd_data(0xD0); lcd_data(0x08); lcd_data(0x10); lcd_data(0x08);
    lcd_data(0x06); lcd_data(0x06); lcd_data(0x39); lcd_data(0x44);
    lcd_data(0x51); lcd_data(0x0B); lcd_data(0x16); lcd_data(0x14);
    lcd_data(0x2F); lcd_data(0x31);
    
    // Display inversion ON (helps with colors)
    lcd_cmd(0x21);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Display ON
    lcd_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(120));
    
    // Clear screen to black
    //fb_clear(0x0000);
  // framebuffer_alloc();
    
      //  lcd_fb_display_framebuffer(0, 0); //yes, we're using this specific fb push here, ebcause its the lcd  not the general other whatever the fucklery 
      ESP_LOGI(TAG, "done");
}
// ──────────────────────────────────────────────
// Refresh wrapper
// ──────────────────────────────────────────────
uint16_t fpsLimiterTarget = 30;  // ← define it here (or extern in header)

void lcd_refresh_screen(void) {
    const uint32_t FRAME_MS = 1000 / fpsLimiterTarget;
    uint32_t frame_start = esp_log_timestamp();

    lcd_fb_display_framebuffer(true, false);  // delta preferred

    uint32_t frame_time = esp_log_timestamp() - frame_start;
    if (frame_time < FRAME_MS) {
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS - frame_time));
    }
}
// --------------------- DRAWING ---------------------


  void fb_rect( 
	bool isfilled,
    uint16_t borderThickness,
    int x, int y, int w, int h,
    uint16_t color,
    uint16_t secondarycolor
) {
    // Clamp
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    // Clamp border thickness
    if (borderThickness * 2 > w) borderThickness = w / 2;
    if (borderThickness * 2 > h) borderThickness = h / 2;

    // ----- DRAW BORDER (if any) -----
    if (borderThickness > 0) {
        // Top + bottom
        for (int py = y; py < y + borderThickness; py++) {
            for (int px = x; px < x + w; px++)
                framebuffer[py * SCREEN_W + px] = secondarycolor;
            mark_rows_range_dirty(true, py);
        }
        for (int py = y + h - borderThickness; py < y + h; py++) {
            for (int px = x; px < x + w; px++)
                framebuffer[py * SCREEN_W + px] = secondarycolor;
            mark_rows_range_dirty(true, py);
        }

        // Left + right
        for (int py = y + borderThickness; py < y + h - borderThickness; py++) {
            for (int px = x; px < x + borderThickness; px++)
                framebuffer[py * SCREEN_W + px] = secondarycolor;
            for (int px = x + w - borderThickness; px < x + w; px++)
                framebuffer[py * SCREEN_W + px] = secondarycolor;
            mark_rows_range_dirty(true, py);
        }
    }

    // ----- DRAW FILL -----
    if (isfilled) {
        int fx = x + borderThickness;
        int fy = y + borderThickness;
        int fw = w - borderThickness * 2;
        int fh = h - borderThickness * 2;

        if (fw > 0 && fh > 0) {
            for (int py = fy; py < fy + fh; py++) {
                uint16_t *dst = &framebuffer[py * SCREEN_W + fx];
                for (int i = 0; i < fw; i++)
                    dst[i] = color;
                mark_rows_range_dirty(true, py);
            }
        }
    } else {
        // Even if hollow, rows are dirty because border touched them
        for (int py = y; py < y + h; py++)
            mark_rows_range_dirty(true, py);
    }
}

  void fb_line(
    int x0, int y0,
    int x1, int y1,
    uint16_t color
) {
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    int miny = y0, maxy = y0;

    while (1) {
        if (x0 >= 0 && x0 < SCREEN_W && y0 >= 0 && y0 < SCREEN_H)
            framebuffer[y0 * SCREEN_W + x0] = color;

        if (y0 < miny) miny = y0;
        if (y0 > maxy) maxy = y0;

        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }

    mark_rows_range_dirty(miny, maxy);
}
  void fb_circle(
    int cx, int cy, int r,
    shapefillpattern mode,
    uint16_t fillColor,
    uint16_t borderColor
) {
    int miny = cy - r;
    int maxy = cy + r;

    for (int y = -r; y <= r; y++) {
        int yy = cy + y;
        if (yy < 0 || yy >= SCREEN_H) continue;

        int dx = (int)sqrtf((float)(r*r - y*y));
        int x0 = cx - dx;
        int x1 = cx + dx;

        if (mode != border_only) {
            if (x0 < 0) x0 = 0;
            if (x1 >= SCREEN_W) x1 = SCREEN_W - 1;
            for (int x = x0; x <= x1; x++)
                framebuffer[yy * SCREEN_W + x] = fillColor;
        }

        if (mode != plain) {
            if (cx - dx >= 0 && cx - dx < SCREEN_W)
                framebuffer[yy * SCREEN_W + (cx - dx)] = borderColor;
            if (cx + dx >= 0 && cx + dx < SCREEN_W)
                framebuffer[yy * SCREEN_W + (cx + dx)] = borderColor;
        }
    }

    mark_rows_range_dirty(miny, maxy);
}
  void fb_triangle(
    int x0,int y0,
    int x1,int y1,
    int x2,int y2,
    shapefillpattern mode,
    uint16_t fillColor,
    uint16_t borderColor
) {
    if (mode != plain) {
        fb_line(x0,y0,x1,y1,borderColor);
        fb_line(x1,y1,x2,y2,borderColor);
        fb_line(x2,y2,x0,y0,borderColor);
    }
    if (mode == border_only) return;

    // sort by y
    if (y1 < y0) { int t=x0;x0=x1;x1=t; t=y0;y0=y1;y1=t; }
    if (y2 < y0) { int t=x0;x0=x2;x2=t; t=y0;y0=y2;y2=t; }
    if (y2 < y1) { int t=x1;x1=x2;x2=t; t=y1;y1=y2;y2=t; }

    float dx01 = (y1-y0)?(float)(x1-x0)/(y1-y0):0;
    float dx02 = (y2-y0)?(float)(x2-x0)/(y2-y0):0;
    float dx12 = (y2-y1)?(float)(x2-x1)/(y2-y1):0;

    float xl = x0, xr = x0;

    for (int y = y0; y <= y1; y++) {
        int a = (int)xl, b = (int)xr;
        if (a > b) { int t=a;a=b;b=t; }
        for (int x=a; x<=b; x++)
            if ((unsigned)x<SCREEN_W && (unsigned)y<SCREEN_H)
                framebuffer[y*SCREEN_W+x] = fillColor;
        xl += dx01;
        xr += dx02;
    }

    xl = x1;
    for (int y = y1; y <= y2; y++) {
        int a = (int)xl, b = (int)xr;
        if (a > b) { int t=a;a=b;b=t; }
        for (int x=a; x<=b; x++)
            if ((unsigned)x<SCREEN_W && (unsigned)y<SCREEN_H)
                framebuffer[y*SCREEN_W+x] = fillColor;
        xl += dx12;
        xr += dx02;
    }

    mark_rows_range_dirty(y0, y2);
}
  void fb_ngon(
    int cx,int cy,int r,uint8_t sides,
    shapefillpattern mode,
    uint16_t fillColor,
    uint16_t borderColor
) {
    if (sides < 3) return;

    int px[sides], py[sides];
    float step = 2.0f * M_PI / sides;

    for (int i=0;i<sides;i++) {
        px[i] = cx + cosf(i*step) * r;
        py[i] = cy + sinf(i*step) * r;
    }

    if (mode != plain) {
        for (int i=0;i<sides;i++)
            fb_line(px[i],py[i],px[(i+1)%sides],py[(i+1)%sides],borderColor);
    }
    if (mode == border_only) return;

    int miny = SCREEN_H-1, maxy = 0;
    for (int i=0;i<sides;i++) {
        if (py[i] < miny) miny = py[i];
        if (py[i] > maxy) maxy = py[i];
    }

    for (int y = miny; y <= maxy; y++) {
        int nodes = 0;
        int nodeX[sides];

        for (int i=0,j=sides-1;i<sides;j=i++) {
            if ((py[i]<y && py[j]>=y) || (py[j]<y && py[i]>=y)) {
                nodeX[nodes++] =
                    px[i] + (y-py[i])*(px[j]-px[i])/(py[j]-py[i]);
            }
        }

        for (int i=0;i<nodes-1;i++)
            for (int j=i+1;j<nodes;j++)
                if (nodeX[i]>nodeX[j]) { int t=nodeX[i];nodeX[i]=nodeX[j];nodeX[j]=t; }

        for (int i=0;i<nodes;i+=2)
            for (int x=nodeX[i]; x<=nodeX[i+1]; x++)
                if ((unsigned)x<SCREEN_W && (unsigned)y<SCREEN_H)
                    framebuffer[y*SCREEN_W+x] = fillColor;
    }

    mark_rows_range_dirty(miny, maxy);
}


/*
static  void fb_rect_gradient(int x, int y, int w, int h, 
                                    uint16_t color_top, uint16_t color_bottom) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    
    // Extract RGB components
    int r1 = (color_top >> 11) & 0x1F;
    int g1 = (color_top >> 5) & 0x3F;
    int b1 = color_top & 0x1F;
    
    int r2 = (color_bottom >> 11) & 0x1F;
    int g2 = (color_bottom >> 5) & 0x3F;
    int b2 = color_bottom & 0x1F;
    
    // Pre-calculate per-row colors
    uint16_t row_colors[h];
    for (int row = 0; row < h; row++) {
        float t = (float)row / (h - 1);
        int r = (int)(r1 + (r2 - r1) * t + 0.5f);
        int g = (int)(g1 + (g2 - g1) * t + 0.5f);
        int b = (int)(b1 + (b2 - b1) * t + 0.5f);
        
        // Clamp and pack
        if (r < 0) r = 0; if (r > 31) r = 31;
        if (g < 0) g = 0; if (g > 63) g = 63;
        if (b < 0) b = 0; if (b > 31) b = 31;
        
        row_colors[row] = (r << 11) | (g << 5) | b;
    }
    
    // Draw with per-row colors (optimized for even widths)
    if ((w & 1) == 0) {
        // Even width - use 32-bit writes
        for (int py = 0; py < h; py++) {
            uint16_t *dst = &framebuffer[(y + py) * SCREEN_W + x];
            uint32_t color32 = ((uint32_t)row_colors[py] << 16) | row_colors[py];
            uint32_t *dst32 = (uint32_t*)dst;
            int w32 = w / 2;
            
            for (int i = 0; i < w32; i++) {
                dst32[i] = color32;
            }
            dd_changedrows_bm_setrowstate(true, y + py);
        }
    } else {
        // Odd width - 16-bit writes
        for (int py = 0; py < h; py++) {
            uint16_t *dst = &framebuffer[(y + py) * SCREEN_W + x];
            uint16_t color16 = row_colors[py];
            
            for (int i = 0; i < w; i++) {
                dst[i] = color16;
            }
            dd_changedrows_bm_setrowstate(true, y + py);
        }
    }
}*/

  void fb_putpixel_225_fakerot(
    int x, int y,
    int ox, int oy,
    uint8_t angle,
    uint16_t color
) {
    int px = ox, py = oy;

    switch (angle & 0x1F) {
        case 0:   px = x;       py = y;       break; // 0°
        case 4:   px = x - oy;  py = y + ox;  break; // 90°
        case 8:   px = x - ox;  py = y - oy;  break; // 180°
        case 12:  px = x + oy;  py = y - ox;  break; // 270°
        default:  px = x;       py = y;       break;
    }

    if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H)
        framebuffer[py * SCREEN_W + px] = color;
}
//0: no angle 1: 22.5 deg 2: 45 deg 3: 67.5 4: 90 deg and so on up to 360




  void fb_draw_text(
    uint8_t angle,
    int x, int y,
    const char* str,
    uint16_t color,
    uint8_t size,
   // text_modifier* modifiers,
    const uint8_t* font,
    uint8_t transparency,
    bool drawblocksforbackground,
    uint16_t blockBackground_color,
    uint16_t maxTLenBeforeAutoWrapToNextLine,
    struct fontcharsize  fontSize
) {
	
    int ax=1, ay=0;   // advance
    int ux=0, uy=1;   // up (glyph rows)

    switch (angle & 0x1F) {
        case 0:  ax=1; ay=0;  ux=0;  uy=1;  break;
        case 4:  ax=0; ay=1;  ux=-1; uy=0;  break;
        case 8:  ax=-1;ay=0;  ux=0;  uy=-1; break;
        case 12: ax=0; ay=-1; ux=1;  uy=0;  break;
        default: ax=1; ay=0;  ux=0;  uy=1;  break;
    }

    int cursor = 0;
	
    while (*str) {
        char c = *str++;
        if (c < ' ' || c > '~') {
            cursor++;
            continue;
        }

        const uint8_t* glyph =
            &font[(c - ' ') * fontSize.x];

        for (int col = 0; col < fontSize.x; col++) {
            uint8_t bits = glyph[col];

            int row = 0;
            while (row < fontSize.y) {
                if (!(bits & (1 << row))) {
                    row++;
                    continue;
                }

                int start = row;
                while (row < fontSize.y && (bits & (1 << row)))
                    row++;

                int end = row - 1;

                // scaled extents
                int span_len = (end - start + 1) * size;

                // base pixel position (unrotated glyph space)
                int gx = (cursor * fontSize.x + col) * size;
                int gy = start * size;

                // convert to screen space
                int px = x + gx * ax + gy * ux;
                int py = y + gx * ay + gy * uy;

                // draw span
for (int s = 0; s < span_len; s++) {
    for (int t = 0; t < size; t++) {
        int sx = px + s * ux + t * ax;
        int sy = py + s * uy + t * ay;

        if (sx >= 0 && sx < SCREEN_W &&
            sy >= 0 && sy < SCREEN_H)
            framebuffer[sy * SCREEN_W + sx] = color;
    }
}



mark_rows_range_dirty(
    py < py + uy*span_len ? py : py + uy*span_len,
    py > py + uy*span_len ? py : py + uy*span_len
);

            }
        }

        cursor++;
    }
}

// --------------------- LCD INIT ---------------------

#define lcd_c_adr_set 0x2A

// --------------------- GLOBALS ---------------------
uint32_t frame = 0;

// --------------------- REFRESH ---------------------

 void lcd_refreshScreen(void) {
    const uint32_t FRAME_MS = 1000 / fpsLimiterTarget; // 45 FPS
    uint32_t frame_start = esp_log_timestamp();
    static uint32_t stats_frame = 0;
    
    // More detailed SPI timing
    static uint32_t spi_total_time = 0;
    static uint32_t spi_transaction_count = 0;
    static uint32_t max_spi_time = 0;
    
    // Swap buffers would go here if we were doing double buffers, but we do not
    
    
    // Time the actual SPI transfers
    uint32_t spi_start = esp_log_timestamp();
    lcd_fb_display_framebuffer(1, 0); //note howit says LCD refresh screen, we will use lcd driver
    uint32_t spi_time = esp_log_timestamp() - spi_start;
    
    // Update SPI stats
    spi_total_time += spi_time;
    spi_transaction_count++;
    if (spi_time > max_spi_time) max_spi_time = spi_time;
    
    taskYIELD();
    
    uint32_t frame_time = esp_log_timestamp() - frame_start;
    
    // Print comprehensive stats
    if (stats_frame % 60 == 0) {  // Every ~1.3 seconds at 45 FPS
        if (spi_transaction_count > 0) {
            uint32_t avg_spi_time = spi_total_time / spi_transaction_count;
            
            ESP_LOGI(TAG, "=== Performance Report ===");
            ESP_LOGI(TAG, "Frame: %lu | Frame time: %lu ms", frame, frame_time);
            ESP_LOGI(TAG, "SPI: Avg %lu ms | Max %lu ms | Count %lu", 
                    avg_spi_time, max_spi_time, spi_transaction_count);
            ESP_LOGI(TAG, "FPS: Current %.1f | Target 45.0", 1000.0f / frame_time);
            
            // Calculate data rate
            uint32_t pixels_per_frame = SCREEN_W * SCREEN_H;
            uint32_t bytes_per_frame = pixels_per_frame * 2;
            float data_rate_mbps = (bytes_per_frame * 8.0f) / (spi_time * 1000.0f);
            ESP_LOGI(TAG, "SPI Data rate: %.2f Mbps", data_rate_mbps);
            
            // Reset stats
            spi_total_time = 0;
            spi_transaction_count = 0;
            max_spi_time = 0;
        }
    }
    stats_frame++;
    
    // Frame pacing
    if (frame_time < FRAME_MS) {
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS - frame_time));
    }

    frame++;
}
