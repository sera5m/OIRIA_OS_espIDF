


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
//#include <stdint>
#include <string.h>
#include <math.h>
#include "rom/cache.h"
#include "driver/spi_common.h"
#define TAG "ST7789_DMA"


//my libs
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"

// --------------------- PINS ---------------------
#define PIN_BL   1
#define PIN_RST  5
#define PIN_DC   4
#define PIN_CS   10
#define PIN_CLK  12
#define PIN_MOSI 11

// --------------------- DISPLAY ---------------------
#define SCREEN_W 240
#define SCREEN_H 280
#define X_OFFSET 0
#define Y_OFFSET 20
#define CHUNK_SIZE 4096


// --------------------- GLOBAL STATE ---------------------

uint16_t lcd_background_color= 0x0000;


static spi_device_handle_t spi;

static uint16_t *framebuffer = NULL;
static uint16_t *framebuffer_front = NULL;
static uint16_t *framebuffer_back  = NULL;

//static bool using_psram = true;
static volatile bool backbuffer_ready = false;
static volatile bool swap_pending = false;

static struct {
    uint32_t chunk_offset;
    bool chunking_active;
    spi_transaction_t trans;
} dma_state = {0};


#define ddCHANGEDROWS_BITMASK_BITS 288
#define ddCHANGEDROWS_BITMASK_BYTES (ddCHANGEDROWS_BITMASK_BITS/8) // 36 bytes

uint8_t dd_changedrows_bitmask[ddCHANGEDROWS_BITMASK_BYTES];

static inline void dd_changedrows_bm_setrowstate(bool changed, uint16_t row) {
    if (changed)
        dd_changedrows_bitmask[row / 8] |=  (1 << (row % 8));  // set bit
    else
        dd_changedrows_bitmask[row / 8] &= ~(1 << (row % 8));  // clear bit
}

static inline bool dd_changedrows_bm_getrowstate(uint16_t row) {
    return (dd_changedrows_bitmask[row / 8] >> (row % 8)) & 1;
}

static inline void dd_changedrows_bm_setallrowschanged(bool state) {
    memset(dd_changedrows_bitmask, state ? 0xFF : 0x00, ddCHANGEDROWS_BITMASK_BYTES);
}
static inline void mark_rows_dirty(int y0, int y1) {
    if (y0 < 0) y0 = 0;
    if (y1 >= SCREEN_H) y1 = SCREEN_H - 1;
    for (int y = y0; y <= y1; y++)
        dd_changedrows_bm_setrowstate(true, y);
}


// --------------------- SPI / GPIO ---------------------
static void spi_init_dma(void)
{
    gpio_config_t cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL<<PIN_DC) | (1ULL<<PIN_RST) | (1ULL<<PIN_BL)
    };
    gpio_config(&cfg);

    gpio_set_direction(PIN_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_CS, 1);
    gpio_set_level(PIN_RST, 1);
    gpio_set_level(PIN_DC, 0);
    gpio_set_level(PIN_BL, 1);

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SCREEN_W * SCREEN_H * 2,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
        .intr_flags = ESP_INTR_FLAG_IRAM
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 76 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 3,
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY,
    };

    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &devcfg, &spi));
}

// --------------------- LCD LOW LEVEL ---------------------
static inline void lcd_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,
        .flags = SPI_TRANS_USE_TXDATA,
    };
    t.tx_data[0] = cmd;
    gpio_set_level(PIN_DC, 0);
    spi_device_polling_transmit(spi, &t);
}

static inline void lcd_data(uint8_t data)
{
    spi_transaction_t t = {
        .length = 8,
        .flags = SPI_TRANS_USE_TXDATA,
    };
    t.tx_data[0] = data;
    gpio_set_level(PIN_DC, 1);
    spi_device_polling_transmit(spi, &t);
}

static inline void lcd_data_bulk(const void *data, size_t len)
{
    if (!len) return;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    gpio_set_level(PIN_DC, 1);
    spi_device_polling_transmit(spi, &t);
}

static inline void lcd_set_window(uint16_t x0, uint16_t y0,
                                  uint16_t x1, uint16_t y1)
{
    uint8_t data[4];
    
    // Apply offsets to center 240x280 in 240x320
    uint16_t px0 = x0 + X_OFFSET;
    uint16_t px1 = x1 + X_OFFSET;
    uint16_t py0 = y0 + Y_OFFSET;  // This centers 280 rows in 320
    uint16_t py1 = y1 + Y_OFFSET;
    
    // Column address set (240 columns)
    lcd_cmd(0x2A);
    data[0] = (px0 >> 8) & 0xFF; data[1] = px0 & 0xFF;
    data[2] = (px1 >> 8) & 0xFF; data[3] = px1 & 0xFF;
    lcd_data_bulk(data, 4);
    
    // Row address set (320 rows total, we use rows 20-299)
    lcd_cmd(0x2B);
    data[0] = (py0 >> 8) & 0xFF; data[1] = py0 & 0xFF;
    data[2] = (py1 >> 8) & 0xFF; data[3] = py1 & 0xFF;
    lcd_data_bulk(data, 4);
    
    // Memory write
    lcd_cmd(0x2C);
}

// --------------------- FRAMEBUFFERS ---------------------
static void fb_init_double_buffer(void)
{
    size_t sz = SCREEN_W * SCREEN_H * sizeof(uint16_t);

    framebuffer_front = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    framebuffer_back  = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);

    if (!framebuffer_front || !framebuffer_back) {
        framebuffer = framebuffer_front ? framebuffer_front : framebuffer_back;
        return;
    }

    framebuffer = framebuffer_back;
    memset(framebuffer_front, 0, sz);
    memset(framebuffer_back, 0, sz);
}

// --------------------- DMA DISPLAY ---------------------
/*
static void fb_display_backbuffer_chunked(void)
{
    spi_transaction_t *ret;

    // Drain completed transaction if any
    if (swap_pending) {
        if (spi_device_get_trans_result(spi, &ret, 0) != ESP_OK) {
            return; // DMA still busy
        }
    }

    if (!dma_state.chunking_active) {
        lcd_set_window(0, 0, SCREEN_W - 1, SCREEN_H - 1);
        dma_state.chunk_offset = 0;
        dma_state.chunking_active = true;
    }

    uint32_t total = SCREEN_W * SCREEN_H * 2;
    if (dma_state.chunk_offset >= total) {
        dma_state.chunking_active = false;
        swap_pending = false;
        backbuffer_ready = false;
        return;
    }

    uint32_t remain = total - dma_state.chunk_offset;
    uint32_t send = remain > CHUNK_SIZE ? CHUNK_SIZE : remain;

    dma_state.trans.length = send * 8;
    dma_state.trans.tx_buffer =
        (uint8_t *)framebuffer_front + dma_state.chunk_offset;
    dma_state.trans.flags = 0;

    gpio_set_level(PIN_DC, 1);
    spi_device_queue_trans(spi, &dma_state.trans, portMAX_DELAY);

    dma_state.chunk_offset += send;
    swap_pending = true;
}*/

static void fb_display_backbuffer_chunk(bool OnlyRenderDelta, bool cope_mode){
	
    const uint32_t row_bytes = SCREEN_W * 2;      // bytes per row
    const uint32_t max_rows_per_chunk = CHUNK_SIZE / row_bytes;

    if (max_rows_per_chunk == 0)
        return; // impossible config

    if (OnlyRenderDelta) {
		
        int y = 0;

        while (y < SCREEN_H) {

            // find first dirty row
            if (!dd_changedrows_bm_getrowstate(y)) {
                y++;
                continue;
            }

            // determine contiguous dirty run
            int y_start = y;
            int y_end = y;

            while (y_end + 1 < SCREEN_H &&
                   dd_changedrows_bm_getrowstate(y_end + 1) &&
                   (y_end - y_start + 1) < max_rows_per_chunk) {
                y_end++;
            }

            const uint32_t rows = y_end - y_start + 1;
            const uint32_t bytes_to_send = rows * row_bytes;

            // framebuffer pointer MUST match window exactly
            uint8_t *buf = (uint8_t *)&framebuffer_front[y_start * SCREEN_W];

            // window must match rows sent — NO OFFSETS HERE
            lcd_set_window(
                0,
                y_start,
                SCREEN_W - 1,
                y_end
            );

            gpio_set_level(PIN_DC, 1);

            spi_transaction_t t = {
                .length    = bytes_to_send * 8,
                .tx_buffer = buf,
                .flags     = 0
            };

            spi_device_transmit(spi, &t);

            // mark rows clean
            for (int ry = y_start; ry <= y_end; ry++) {
                dd_changedrows_bm_setrowstate(false, ry);
            }

            y = y_end + 1;
        }
        taskYIELD(); 

    } else {
		
        // FULL FRAME — ALSO ROW ALIGNED
        for (int y = 0; y < SCREEN_H; y += max_rows_per_chunk) {

            int rows = max_rows_per_chunk;
            if (y + rows > SCREEN_H)
                rows = SCREEN_H - y;

            uint8_t *buf = (uint8_t *)&framebuffer_front[y * SCREEN_W];
            uint32_t bytes = rows * row_bytes;

            lcd_set_window(
                0,
                y,
                SCREEN_W - 1,
                y + rows - 1
            );

            gpio_set_level(PIN_DC, 1);

            spi_transaction_t t = {
                .length    = bytes * 8,
                .tx_buffer = buf,
                .flags     = 0
            };

            spi_device_transmit(spi, &t);
        }

        dd_changedrows_bm_setallrowschanged(false);
        
    }//end else

    backbuffer_ready = false;
    }





static void fb_display_backbuffer_pump(void)
{
    spi_transaction_t *ret;
    const uint32_t total = SCREEN_W * SCREEN_H * 2;

    // Drain previous transaction
    if (swap_pending) {
        if (spi_device_get_trans_result(spi, &ret, 0) != ESP_OK) {
            return; // previous DMA still in flight
        }
    }

    if (!backbuffer_ready) return;  // nothing to display

    // Start chunking if first call
    if (!dma_state.chunking_active) {
        lcd_set_window(0, 0, SCREEN_W - 1, SCREEN_H - 1);
        dma_state.chunk_offset = 0;
        dma_state.chunking_active = true;
        swap_pending = false;
    }

    // Queue next chunk if any remaining
    if (dma_state.chunk_offset < total) {
        uint32_t remain = total - dma_state.chunk_offset;
        uint32_t send = remain > CHUNK_SIZE ? CHUNK_SIZE : remain;

        dma_state.trans.length = send * 8;
        dma_state.trans.tx_buffer =
            (uint8_t *)framebuffer_front + dma_state.chunk_offset;
        dma_state.trans.flags = 0;

        gpio_set_level(PIN_DC, 1);
        if (spi_device_queue_trans(spi, &dma_state.trans, 0) == ESP_OK) {
            dma_state.chunk_offset += send;
            swap_pending = true;
        }
    }

    // Frame complete
    if (dma_state.chunk_offset >= total) {
        dma_state.chunking_active = false;
        backbuffer_ready = false;
        dma_state.chunk_offset = 0;
    }
}

static bool fb_display_done_chunked(void)
{
    if (!swap_pending) return true;

    spi_transaction_t *ret;
    if (spi_device_get_trans_result(spi, &ret, 0) == ESP_OK) {
        uint32_t total = SCREEN_W * SCREEN_H * 2;
        if (dma_state.chunk_offset >= total) {
            dma_state.chunking_active = false;
            swap_pending = false;
            backbuffer_ready = false;
            return true;
        }
        fb_display_backbuffer_chunk(1,1);
    }
    return false;
}

static void fb_swap_buffers(void)
{
    while (swap_pending) fb_display_done_chunked();

    dma_state.chunking_active = false;
    dma_state.chunk_offset = 0;

    uint16_t *t = framebuffer_front;
    framebuffer_front = framebuffer_back;
    framebuffer_back = t;
    framebuffer = framebuffer_back;

    backbuffer_ready = true;
}

// --------------------- DRAWING ---------------------
static inline void fb_clear(uint16_t color)
{
    uint32_t c = (color<<16)|color;
    uint32_t *p = (uint32_t*)framebuffer;
    size_t n = (SCREEN_W*SCREEN_H)/2;
    for (size_t i=0;i<n;i++){ p[i]=c;}
    dd_changedrows_bm_setallrowschanged(1);
}

typedef enum{
	plain,border_only,plainAndBorder,
	staticky_plain,dots,
	triangletiling,diamondtiling,checkerboard,circles, //shape tiling
	lines,waves,concentric_layers_gradinent_ofthisshape,//more patterns
	topographic_fakery,circut_fakery,honeycomb //cooler patterns
	}shapefillpattern;

typedef enum {
    none, strikethrough, underlined, italicized,
    bold, transparent, highlighted
} text_modifier;


static inline void fb_rect( 
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
            dd_changedrows_bm_setrowstate(true, py);
        }
        for (int py = y + h - borderThickness; py < y + h; py++) {
            for (int px = x; px < x + w; px++)
                framebuffer[py * SCREEN_W + px] = secondarycolor;
            dd_changedrows_bm_setrowstate(true, py);
        }

        // Left + right
        for (int py = y + borderThickness; py < y + h - borderThickness; py++) {
            for (int px = x; px < x + borderThickness; px++)
                framebuffer[py * SCREEN_W + px] = secondarycolor;
            for (int px = x + w - borderThickness; px < x + w; px++)
                framebuffer[py * SCREEN_W + px] = secondarycolor;
            dd_changedrows_bm_setrowstate(true, py);
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
                dd_changedrows_bm_setrowstate(true, py);
            }
        }
    } else {
        // Even if hollow, rows are dirty because border touched them
        for (int py = y; py < y + h; py++)
            dd_changedrows_bm_setrowstate(true, py);
    }
}

static inline void fb_line(
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

    mark_rows_dirty(miny, maxy);
}
static inline void fb_circle(
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

    mark_rows_dirty(miny, maxy);
}
static inline void fb_triangle(
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

    mark_rows_dirty(y0, y2);
}
static inline void fb_ngon(
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

    mark_rows_dirty(miny, maxy);
}


/*
static inline void fb_rect_gradient(int x, int y, int w, int h, 
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

static inline void fb_putpixel_225_fakerot(
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



struct vec2_ui16t{
	 uint16_t x;
	 uint16_t y;
	};
static inline void fb_draw_text(
    uint8_t angle,
    int x, int y,
    const char* str,
    uint16_t color,
    uint8_t size,
    text_modifier* modifiers,
    const uint8_t* font,
    bool wraptext,
    bool drawblocksforbackground,
    uint16_t blockBackground_color,
    uint16_t maxTLenBeforeAutoWrapToNextLine,
    struct vec2_ui16t fontSize
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
                    int sx = px + s * ux;
                    int sy = py + s * uy;

                    if (sx>=0 && sx<SCREEN_W &&
                        sy>=0 && sy<SCREEN_H)
                        framebuffer[sy*SCREEN_W + sx] = color;
                }

                mark_rows_dirty(
                    py < py + uy*span_len ? py : py + uy*span_len,
                    py > py + uy*span_len ? py : py + uy*span_len
                );
            }
        }

        cursor++;
    }
}

// --------------------- LCD INIT ---------------------
static void lcd_init_simple(void)
{
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Software reset
    lcd_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(150));

    // Sleep out
    lcd_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));

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
    
    // ---------- CRITICAL FOR 240x320 ----------
    // Set display resolution to 240x320
    lcd_cmd(0x2A);  // Column address set
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
    fb_clear(0x5555);
    fb_swap_buffers();
    while (backbuffer_ready) {
        fb_display_backbuffer_chunk(0, 0);
    }
}
// --------------------- GLOBALS ---------------------
static uint32_t frame = 0;

// --------------------- REFRESH ---------------------


static void refreshScreen(void) {
    const uint32_t FRAME_MS = 1000 / 45; // 45 FPS
    uint32_t frame_start = esp_log_timestamp();
    static uint32_t stats_frame = 0;
    
    // More detailed SPI timing
    static uint32_t spi_total_time = 0;
    static uint32_t spi_transaction_count = 0;
    static uint32_t max_spi_time = 0;
    
    // Swap buffers
    fb_swap_buffers();
    
    // Time the actual SPI transfers
    uint32_t spi_start = esp_log_timestamp();
    fb_display_backbuffer_chunk(1, 0);
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

// --------------------- MAIN ---------------------
void app_main_cubedemo(void){
    spi_init_dma();
    fb_init_double_buffer();
    lcd_init_simple();

    int x1 = 50, y1 = 50, vx1 = 3, vy1 = 2;
    int x2 = 120, y2 = 100, vx2 = -2, vy2 = 3;
    const int size = 40;

    while (1) {
        // Clear screen
       fb_clear(lcd_background_color);

        // Draw squares
        fb_rect(1, 0, x1, y1, size, size, 0xF800,0xF1FF ); 
        fb_rect(0,4,x2, y2, size, size, 0x07E0, 0xF00F);  // green

        // Update positions
        x1 += vx1; y1 += vy1;
        
        x2 += vx2; y2 += vy2;

        // Bounce off walls
        if (x1 <= 0 || x1 + size >= SCREEN_W) vx1 = -vx1;
        if (y1 <= 0 || y1 + size >= SCREEN_H) vy1 = -vy1;
        if (x2 <= 0 || x2 + size >= SCREEN_W) vx2 = -vx2;
        if (y2 <= 0 || y2 + size >= SCREEN_H) vy2 = -vy2;

       refreshScreen();
       // last_frame = esp_log_timestamp();
    }
}
void app_main_shapedemo(void)
{
    spi_init_dma();
    fb_init_double_buffer();
    lcd_init_simple();

    int frame = 0;
    int x = 40, y = 40;
    int vx = 2, vy = 1;

    while (1) {
        fb_clear(lcd_background_color);

        shapefillpattern mode;
        switch ((frame / 60) % 3) {
            case 0: mode = plain; break;
            case 1: mode = border_only; break;
            default: mode = plainAndBorder; break;
        }

        // animate position
        x += vx; y += vy;
        if (x < 20 || x > SCREEN_W - 100) vx = -vx;
        if (y < 20 || y > SCREEN_H - 100) vy = -vy;

        /* ========= RECT ========= */
        fb_rect(
            mode != border_only,
            mode == border_only ? 3 : 3,
            x, y,
            60, 40,
            0x07E0,        // fill (green)
            0xF800         // border (red)
        );

        /* ========= LINE ========= */
        fb_line(
            10, SCREEN_H - 20,
            SCREEN_W - 10, 20,
            0xFFFF
        );

        /* ========= CIRCLE ========= */
        fb_circle(
            SCREEN_W / 2, SCREEN_H / 2,
            30,
            mode,
            0x001F,        // fill (blue)
            0xFFE0         // border (yellow)
        );

        /* ========= TRIANGLE ========= */
        fb_triangle(
            200, 40,
            260, 100,
            180, 100,
            mode,
            0xF81F,        // magenta fill
            0xFFFF         // white border
        );

        /* ========= N-GON (hexagon) ========= */
        fb_ngon(
            100, SCREEN_H - 80,
            30,
            frame,
            mode,
            0x07FF,        // cyan fill
            0xF800         // red border
        );

        refreshScreen();
        frame++;
    }
}
void app_main(void){
    spi_init_dma();
    fb_init_double_buffer();
    lcd_init_simple();

    int x1 = 50, y1 = 50, vx1 = 3, vy1 = 2;
    int x2 = 120, y2 = 100, vx2 = -2, vy2 = 3;
    const int size = 40;

    const char test_str[] = "HELLO 6x8 FONT";
    struct vec2_ui16t font6x8 = {6, 8};

    while (1) {
        fb_clear(lcd_background_color);

        // moving squares
        fb_rect(1, 0, x1, y1, size, size, 0xF800, 0);
        fb_rect(0, 4, x2, y2, size, size, 0x07E0, 0xF00F);

        // text tests
        fb_draw_text(
            0,                  // 0°
            225, 64,
            "bruh lol",
            0xFFFF,
            1,
            NULL,
            avrclassic_font6x8,
            false,
            false,
            lcd_background_color,
            32,
            font6x8
        );

        fb_draw_text(
            4,                  // 90°
            200, 20,
            "ROT90",
            0xFFE0,
            1,
            NULL,
            avrclassic_font6x8,
            false,
            false,
            lcd_background_color,
            16,
            font6x8
        );

        fb_draw_text(
            8,                  // 180°
            200, 120,
            "UPSIDE",
            0xF81F,
            2,
            NULL,
            avrclassic_font6x8,
            false,
            false,
            lcd_background_color,
            16,
            font6x8
        );
        fb_draw_text(
            16,                  // 180°
            150, 100,
            "FUCK",
            0xF81F,
            2,
            NULL,
            avrclassic_font6x8,
            false,
            false,
            lcd_background_color,
            16,
            font6x8
        );

        // motion update
        x1 += vx1; y1 += vy1;
        x2 += vx2; y2 += vy2;

        if (x1 <= 0 || x1 + size >= SCREEN_W) vx1 = -vx1;
        if (y1 <= 0 || y1 + size >= SCREEN_H) vy1 = -vy1;
        if (x2 <= 0 || x2 + size >= SCREEN_W) vx2 = -vx2;
        if (y2 <= 0 || y2 + size >= SCREEN_H) vy2 = -vy2;

        refreshScreen();
    }
}

