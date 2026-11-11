#include "boot_mode_select.h"
#include "hardware/wiring/wiring.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/encoders/ky040_driver.hpp"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "boot_mode";

// Menu button = encoder0 switch ("sel"). Active low assumed.
#ifndef MENU_BOOT_PIN
#define MENU_BOOT_PIN ENCODER0_SW_PIN
#endif

#define MODE_SELECT_TIMEOUT_S 60
#define HOLD_CONFIRM_MS       200

// ---------------------------------------------------------------------------
// Wake reason
// ---------------------------------------------------------------------------

wake_check_t boot_check_why_awake(void)
{
    wake_check_t w = {0};

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    switch (cause) {
        case ESP_SLEEP_WAKEUP_GPIO:
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
            w.reasons |= WAKE_MSG_GPIO;
            w.force_wake = true;
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            w.reasons |= WAKE_MSG_TIMER;
            break;
        case ESP_SLEEP_WAKEUP_ULP:
            w.reasons |= WAKE_MSG_ULP;
            w.force_wake = true;
            break;
        default:
            break;
    }

    // ULP shared state (optional; symbols may be absent if ULP not linked yet).
    extern uint8_t ulp_wake_main_now __attribute__((weak));
    extern uint8_t ulp_wake_reason   __attribute__((weak));
    if (&ulp_wake_main_now && ulp_wake_main_now) {
        w.reasons |= WAKE_MSG_ULP;
        w.force_wake = true;
        if (&ulp_wake_reason) {
            w.ulp_raw = ulp_wake_reason;
            if (ulp_wake_reason & 0x02) w.reasons |= WAKE_MSG_ALARM;
            if (ulp_wake_reason & 0x04) w.reasons |= WAKE_MSG_TIMER;
            if (ulp_wake_reason & 0x01) w.reasons |= WAKE_MSG_PERIODIC;
        }
    }

    if (w.reasons) {
        ESP_LOGI(TAG, "awake reasons=0x%lx force=%d ulp_raw=0x%02x",
                 (unsigned long)w.reasons, w.force_wake, w.ulp_raw);
    } else {
        ESP_LOGI(TAG, "cold boot / no wake reason");
    }
    return w;
}

// ---------------------------------------------------------------------------
// Menu button
// ---------------------------------------------------------------------------

bool boot_menu_button_held(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << MENU_BOOT_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    int held = 0;
    for (int i = 0; i < HOLD_CONFIRM_MS / 20; i++) {
        if (gpio_get_level(MENU_BOOT_PIN) == 0) held++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    bool pressed = (held >= (HOLD_CONFIRM_MS / 20) / 2);
    ESP_LOGI(TAG, "menu button %s", pressed ? "HELD" : "not held");
    return pressed;
}

// ---------------------------------------------------------------------------
// Mode list
// ---------------------------------------------------------------------------

typedef enum {
    MODE_DEFAULT = 0,
    MODE_COLLAB,
    MODE_HELPER,
    MODE_SHUTDOWN,
    MODE_FLASH,
    MODE_COUNT
} mode_id_t;

static const struct {
    const char *title;
    const char *expl;
    boot_role_t role;
} modes[MODE_COUNT] = {
    { "default",
      "acts as normal watch and controller unit",
      BOOT_ROLE_SOLO },
    { "collaborative",
      "connect another ESP32-S3 or computer over the expansion port or USB, and put it in collaborative mode as well",
      BOOT_ROLE_TYRANT },
    { "helper",
      "this unit acts entirely as an assistant to another unit which acts as control",
      BOOT_ROLE_PUPPET },
    { "shut down",
      "turn this device off",
      BOOT_ROLE_SOLO },
    { "firmware flash",
      "wait for a computer or another unit to flash firmware over the connector/USB/I2C. Or select OTA in the menu app after a normal boot.",
      BOOT_ROLE_SOLO },
};

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

static void draw_mode_screen(int sel, int seconds_left)
{
    fb_clear(0x0000);

    fb_draw_text(0, 4, 4, "MODE SELECT", 0xFFFF, 2,
                 0, true, 0x0000, 240, font_avr_classics);

    char buf[64];
    snprintf(buf, sizeof(buf), "menu held  |  %d s to default", seconds_left);
    fb_draw_text(0, 4, 28, buf, 0x001F, 1,
                 0, true, 0x0000, 240, font_avr_classics);

    int y = 52;
    for (int i = 0; i < MODE_COUNT; i++) {
        uint16_t title_col = (i == sel) ? 0xFFFF : 0x7BEF;
        uint16_t expl_col  = 0x001F;

        char line[48];
        snprintf(line, sizeof(line), "%s %s", (i == sel) ? ">" : " ", modes[i].title);
        fb_draw_text(0, 4, y, line, title_col, 2,
                     0, true, 0x0000, 240, font_avr_classics);
        y += 22;

        fb_draw_text(0, 12, y, modes[i].expl, expl_col, 1,
                     0, true, 0x0000, 220, font_avr_classics);
        y += 28;
    }

    fb_draw_text(0, 4, 260, "scroll to select  |  click to confirm", 0x7BEF, 1,
                 0, true, 0x0000, 240, font_avr_classics);

    lcd_fb_display_framebuffer(false, false);
}

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------

static volatile int s_enc_delta = 0;
static void on_twist(void *ctx, int delta) {
    (void)ctx;
    s_enc_delta += delta;
}

static void commit_role_nvs(boot_role_t role)
{
    nvs_handle_t h;
    if (nvs_open("rshell", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "boot_role", (uint8_t)role);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "NVS boot_role = %s", boot_role_name(role));
}

boot_role_t boot_mode_select_if_held(void)
{
    if (!boot_menu_button_held()) {
        return BOOT_ROLE_SOLO;
    }

    ESP_LOGI(TAG, "entering mode select");

    extern void lcd_init_simple(void);
    lcd_init_simple();
    framebuffer_alloc();

    ky040_config_t cfg = {
        .clk_pin = ENCODER0_CLK_PIN,
        .dt_pin  = ENCODER0_DT_PIN,
        .detents_per_rev = 20,
        .on_twist = on_twist,
        .user_ctx = NULL,
        .use_interrupt = false,
    };
    ky040_handle_t enc = NULL;
    ky040_new(&cfg, &enc);

    int sel = 0;
    int seconds = MODE_SELECT_TIMEOUT_S;
    TickType_t last_sec = xTaskGetTickCount();
    bool saw_release = false;

    draw_mode_screen(sel, seconds);

    while (seconds > 0) {
        if (enc) ky040_poll(enc);

        if (s_enc_delta) {
            sel = (sel + s_enc_delta + MODE_COUNT) % MODE_COUNT;
            s_enc_delta = 0;
            draw_mode_screen(sel, seconds);
        }

        int lvl = gpio_get_level(MENU_BOOT_PIN);
        if (lvl == 1) saw_release = true;
        if (saw_release && lvl == 0) {
            break;
        }

        if ((xTaskGetTickCount() - last_sec) >= pdMS_TO_TICKS(1000)) {
            last_sec = xTaskGetTickCount();
            seconds--;
            draw_mode_screen(sel, seconds);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    mode_id_t chosen = (seconds > 0) ? (mode_id_t)sel : MODE_DEFAULT;
    ESP_LOGI(TAG, "mode select: %s", modes[chosen].title);

    switch (chosen) {
        case MODE_SHUTDOWN:
            fb_clear(0x0000);
            fb_draw_text(0, 20, 120, "shutting down", 0xFFFF, 2,
                         0, true, 0x0000, 200, font_avr_classics);
            lcd_fb_display_framebuffer(false, false);
            vTaskDelay(pdMS_TO_TICKS(800));
            esp_deep_sleep_start();
            break;
        case MODE_FLASH:
            fb_clear(0x0000);
            fb_draw_text(0, 4, 100, "firmware flash", 0xFFFF, 2,
                         0, true, 0x0000, 230, font_avr_classics);
            fb_draw_text(0, 4, 130, "connect USB or another unit", 0x001F, 1,
                         0, true, 0x0000, 230, font_avr_classics);
            lcd_fb_display_framebuffer(false, false);
            commit_role_nvs(BOOT_ROLE_SOLO);
            return BOOT_ROLE_SOLO;
        default:
            commit_role_nvs(modes[chosen].role);
            return modes[chosen].role;
    }
    return BOOT_ROLE_SOLO;
}
