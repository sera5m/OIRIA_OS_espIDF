/*
 * NTSC-M mono scanout. The i80 DMA block shifts a byte per sample on D0..D7.
 * Only D0 (sync) and D1 (luma) are wired to the RCA mixer. The task blocks on
 * DMA completions, so the idle task still runs and the task WDT stays fed.
 */
#include "hardware/drivers/lcd/crt_rca_mono/crt_rm.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "soc/soc_caps.h"

static const char *TAG = "crt_rm";

/* D0 sync, D1 luma. The other six are required by bus_width 8 and stay open. */
static const int s_data_pins[8] = {
    CRT_SYNC_GPIO,
    CRT_LUMA_GPIO,
    GPIO_NUM_26,
    GPIO_NUM_27,
    GPIO_NUM_39,
    GPIO_NUM_40,
    GPIO_NUM_41,
    GPIO_NUM_47,
};

enum {
    LVL_SYNC  = 0x00,
    LVL_BLACK = 0x01,
    LVL_WHITE = 0x03,
};

static esp_lcd_i80_bus_handle_t s_bus;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_free;
static TaskHandle_t s_task;
static uint8_t *s_line[2];
static uint8_t s_mono[2][CRT_ACTIVE_LINES][(240 + 7) / 8];
static volatile int s_shown;
static volatile bool s_run;
static volatile bool s_exited;
static bool s_inited;

static inline bool luma_white(uint16_t c)
{
    unsigned r = (c >> 11) & 31;
    unsigned g = (c >> 5) & 63;
    unsigned b = c & 31;
    return (r * 2 + g + b * 2) >= 48;
}

static bool IRAM_ATTR on_line_done(esp_lcd_panel_io_handle_t io,
                                  esp_lcd_panel_io_event_data_t *edata,
                                  void *user_ctx)
{
    (void)io;
    (void)edata;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_ctx, &hp);
    return hp == pdTRUE;
}

static void build_line(uint8_t *dst, int line, const uint8_t *field)
{
    const int active0 = CRT_VSYNC_LINES + CRT_PRE_BLANK;

    if (line < CRT_VSYNC_LINES) {
        memset(dst, LVL_SYNC, CRT_LINE_SAMPLES);
        memset(dst + CRT_LINE_SAMPLES - 24, LVL_BLACK, 24);
        return;
    }

    memset(dst, LVL_BLACK, CRT_LINE_SAMPLES);
    memset(dst, LVL_SYNC, CRT_HSYNC_SAMPLES);

    if (!field) return;
    if (line < active0 || line >= active0 + CRT_ACTIVE_LINES) return;

    const int ai = line - active0;
    const uint8_t *bits = field + (size_t)ai * ((240 + 7) / 8);
    uint8_t *pix = dst + CRT_HSYNC_SAMPLES + CRT_BACK_PORCH;
    for (int i = 0; i < CRT_OUT_PIXELS; i++) {
        int sx = (i * (SCREEN_W - 1)) / (CRT_OUT_PIXELS - 1);
        int on = (bits[sx >> 3] >> (7 - (sx & 7))) & 1;
        uint8_t v = on ? LVL_WHITE : LVL_BLACK;
        pix[0] = v;
        pix[1] = v;
        pix += CRT_SAMPLES_PER_PX;
    }
}

static void crt_task(void *arg)
{
    (void)arg;
    int line = 0;
    uint32_t submit_n = 0;
    const uint8_t *field = NULL;

    while (s_run) {
        if (xSemaphoreTake(s_free, pdMS_TO_TICKS(20)) != pdTRUE) {
            continue;
        }
        if (!s_run) break;

        if (line == 0) {
            field = &s_mono[s_shown][0][0];
        }
        int idx = (int)(submit_n & 1);
        build_line(s_line[idx], line, field);
        esp_err_t err = esp_lcd_panel_io_tx_color(s_io, LVL_SYNC, s_line[idx], CRT_LINE_SAMPLES);
        if (err != ESP_OK) {
            xSemaphoreGive(s_free);
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        submit_n++;
        if (++line >= CRT_LINES_PER_FIELD) line = 0;
    }

    for (int i = 0; i < 2; i++) {
        xSemaphoreTake(s_free, pdMS_TO_TICKS(20));
    }
    s_exited = true;
    s_task = NULL;
    vTaskDelete(NULL);
}

static void fail_init(void)
{
    if (s_io) {
        esp_lcd_panel_io_del(s_io);
        s_io = NULL;
    }
    if (s_bus) {
        esp_lcd_del_i80_bus(s_bus);
        s_bus = NULL;
    }
    for (int i = 0; i < 2; i++) {
        if (s_line[i]) {
            heap_caps_free(s_line[i]);
            s_line[i] = NULL;
        }
    }
    if (s_free) {
        vSemaphoreDelete(s_free);
        s_free = NULL;
    }
    s_run = false;
    s_inited = false;
}

void crt_rm_init(void)
{
    if (s_inited) return;

    s_free = xSemaphoreCreateCounting(2, 2);
    if (!s_free) {
        ESP_LOGE(TAG, "semaphore alloc failed");
        return;
    }

    for (int i = 0; i < 2; i++) {
        s_line[i] = heap_caps_malloc(CRT_LINE_SAMPLES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (!s_line[i]) {
            ESP_LOGE(TAG, "line buffer alloc failed");
            fail_init();
            return;
        }
        memset(s_line[i], LVL_BLACK, CRT_LINE_SAMPLES);
    }

    esp_lcd_i80_bus_config_t bus_cfg = {
        .dc_gpio_num = GPIO_NUM_NC,
        .wr_gpio_num = CRT_WR_GPIO,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .bus_width = 8,
        .max_transfer_bytes = 1024,
        .psram_trans_align = 64,
        .sram_trans_align = 4,
    };
    for (int i = 0; i < SOC_LCD_I80_BUS_WIDTH && i < 16; i++) {
        bus_cfg.data_gpio_nums[i] = GPIO_NUM_NC;
    }
    for (int i = 0; i < 8; i++) {
        bus_cfg.data_gpio_nums[i] = s_data_pins[i];
    }

    esp_err_t err = esp_lcd_new_i80_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i80 bus: %s", esp_err_to_name(err));
        fail_init();
        return;
    }

    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num = GPIO_NUM_NC,
        .pclk_hz = CRT_PCLK_HZ,
        .trans_queue_depth = 2,
        .on_color_trans_done = on_line_done,
        .user_ctx = s_free,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags = {
            .pclk_idle_low = 1,
        },
    };
    err = esp_lcd_new_panel_io_i80(s_bus, &io_cfg, &s_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i80 io: %s", esp_err_to_name(err));
        fail_init();
        return;
    }

    s_run = true;
    s_exited = false;
    if (xTaskCreatePinnedToCore(crt_task, "crt", 4096, NULL, 12, &s_task, 1) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        fail_init();
        return;
    }
    s_inited = true;
    ESP_LOGI(TAG, "NTSC-M mono RCA, sync=%d luma=%d wr=%d (wr left open)",
             CRT_SYNC_GPIO, CRT_LUMA_GPIO, CRT_WR_GPIO);
}

void crt_rm_stop(void)
{
    if (!s_inited) return;
    s_run = false;
    xSemaphoreGive(s_free);
    for (int i = 0; i < 50 && !s_exited; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    fail_init();
}

void crt_rm_present(void)
{
    if (!s_inited) crt_rm_init();
    if (!s_inited) return;

    const uint16_t *fb = framebuffer_front;
    int back = s_shown ^ 1;
    uint8_t *dst = s_mono[back];
    const int row_bytes = (240 + 7) / 8;

    if (!fb) {
        memset(dst, 0, sizeof(s_mono[0]));
    } else {
        for (int y = 0; y < CRT_ACTIVE_LINES; y++) {
            int sy = (y * (SCREEN_H - 1)) / (CRT_ACTIVE_LINES - 1);
            if (sy < 0) sy = 0;
            if (sy >= SCREEN_H) sy = SCREEN_H - 1;
            const uint16_t *src = fb + (size_t)sy * SCREEN_W;
            uint8_t *row = dst + (size_t)y * row_bytes;
            for (int x = 0; x < SCREEN_W; x += 8) {
                uint8_t b = 0;
                int n = SCREEN_W - x;
                if (n > 8) n = 8;
                for (int k = 0; k < n; k++) {
                    if (luma_white(src[x + k])) b |= (uint8_t)(0x80 >> k);
                }
                row[x >> 3] = b;
            }
        }
    }
    __sync_synchronize();
    s_shown = back;
}

bool crt_rm_running(void)
{
    return s_inited && s_run;
}
