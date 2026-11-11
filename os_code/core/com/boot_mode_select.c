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
#include "driver/uart.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static const char *TAG = "boot_mode";

static wake_check_t s_last_wake = {0};

wake_check_t boot_get_last_wake(void) {
    return s_last_wake;
}

// Menu button = encoder0 switch ("sel"). Active low assumed.
#ifndef MENU_BOOT_PIN
#define MENU_BOOT_PIN ENCODER0_SW_PIN
#endif

#define MODE_SELECT_TIMEOUT_S 60
#define HOLD_CONFIRM_MS       200
#define SERIAL_RESCUE_MS      3000   // listen this long even if button not held

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

    s_last_wake = w;
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
      "wait for a computer or another unit to flash firmware over the connector/USB/I2C. Connect USB, or select OTA in the menu app after a normal boot.",
      BOOT_ROLE_SOLO },
};

// ---------------------------------------------------------------------------
// Serial rescue (dumb UART console, same text later goes to rshell)
// ---------------------------------------------------------------------------

static void serial_write(const char *s)
{
    uart_write_bytes(UART_NUM_0, s, strlen(s));
}

static boot_role_t role_from_name(const char *name)
{
    if (!name) return BOOT_ROLE_SOLO;
    if (!strcasecmp(name, "solo") || !strcasecmp(name, "default") || !strcasecmp(name, "normal"))
        return BOOT_ROLE_SOLO;
    if (!strcasecmp(name, "tyrant") || !strcasecmp(name, "collaborative") || !strcasecmp(name, "collab"))
        return BOOT_ROLE_TYRANT;
    if (!strcasecmp(name, "puppet") || !strcasecmp(name, "helper") || !strcasecmp(name, "assistant"))
        return BOOT_ROLE_PUPPET;
    return (boot_role_t)0xFF;
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

// Returns true if the command wants the early path to exit with *out_role.
bool boot_serial_handle_line(const char *line, boot_role_t *out_role)
{
    if (!line || line[0] != '/') return false;

    // Skip leading '/' and any extra spaces.
    const char *p = line + 1;
    while (*p == ' ') p++;

    // Expect "bootseq" as the first token.
    if (strncasecmp(p, "bootseq", 7) != 0) {
        serial_write("err: unknown prefix (want /bootseq ...)\r\n");
        return false;
    }
    p += 7;
    while (*p == ' ') p++;

    if (strncasecmp(p, "help", 4) == 0 || *p == '\0' || *p == '?') {
        serial_write("early boot commands:\r\n");
        serial_write("  /bootseq help\r\n");
        serial_write("  /bootseq status\r\n");
        serial_write("  /bootseq setconfvar mode <default|collaborative|helper|solo|tyrant|puppet>\r\n");
        serial_write("  /bootseq getconfvar mode\r\n");
        serial_write("  /bootseq shutdown\r\n");
        serial_write("  /bootseq flash\r\n");
        return false;
    }

    if (strncasecmp(p, "status", 6) == 0) {
        char buf[96];
        snprintf(buf, sizeof(buf), "awake=0x%lx force=%d\r\n",
                 (unsigned long)s_last_wake.reasons, s_last_wake.force_wake);
        serial_write(buf);
        return false;
    }

    if (strncasecmp(p, "shutdown", 8) == 0) {
        serial_write("shutting down\r\n");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_deep_sleep_start();
        return true; // never reaches
    }

    if (strncasecmp(p, "flash", 5) == 0) {
        serial_write("firmware flash: connect USB or another unit, then reboot\r\n");
        commit_role_nvs(BOOT_ROLE_SOLO);
        if (out_role) *out_role = BOOT_ROLE_SOLO;
        return true;
    }

    if (strncasecmp(p, "getconfvar", 10) == 0) {
        p += 10;
        while (*p == ' ') p++;
        if (strncasecmp(p, "mode", 4) == 0) {
            // Just report what is currently in NVS (or default).
            nvs_handle_t h;
            uint8_t v = 0xFF;
            if (nvs_open("rshell", NVS_READONLY, &h) == ESP_OK) {
                nvs_get_u8(h, "boot_role", &v);
                nvs_close(h);
            }
            char buf[64];
            if (v <= BOOT_ROLE_PUPPET)
                snprintf(buf, sizeof(buf), "mode=%s\r\n", boot_role_name((boot_role_t)v));
            else
                snprintf(buf, sizeof(buf), "mode=(unset)\r\n");
            serial_write(buf);
        } else {
            serial_write("err: unknown var\r\n");
        }
        return false;
    }

    if (strncasecmp(p, "setconfvar", 10) == 0) {
        p += 10;
        while (*p == ' ') p++;
        if (strncasecmp(p, "mode", 4) == 0) {
            p += 4;
            while (*p == ' ') p++;
            boot_role_t r = role_from_name(p);
            if ((uint8_t)r == 0xFF) {
                serial_write("err: bad mode (default|collaborative|helper)\r\n");
                return false;
            }
            commit_role_nvs(r);
            char buf[64];
            snprintf(buf, sizeof(buf), "ok mode=%s\r\n", boot_role_name(r));
            serial_write(buf);
            if (out_role) *out_role = r;
            return true;
        }
        serial_write("err: unknown var\r\n");
        return false;
    }

    serial_write("err: unknown /bootseq command (try /bootseq help)\r\n");
    return false;
}

// Non-blocking line reader. Returns true when a full line is ready in buf.
static bool serial_read_line(char *buf, size_t cap, int *pos)
{
    uint8_t c;
    int n = uart_read_bytes(UART_NUM_0, &c, 1, 0);
    if (n <= 0) return false;

    if (c == '\r' || c == '\n') {
        if (*pos > 0) {
            buf[*pos] = '\0';
            *pos = 0;
            return true;
        }
        return false;
    }
    if (*pos < (int)cap - 1 && c >= 32 && c < 127) {
        buf[(*pos)++] = (char)c;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Draw (local LCD path)
// ---------------------------------------------------------------------------

static void draw_mode_screen(int sel, int seconds_left)
{
    fb_clear(0x0000);

    fb_draw_text(0, 4, 2, "MODE SELECT", 0xFFFF, 2,
                 0, true, 0x0000, 240, font_avr_classics);

    char buf[64];
    snprintf(buf, sizeof(buf), "%d s  |  /bootseq on serial", seconds_left);
    fb_draw_text(0, 4, 22, buf, 0x001F, 1,
                 0, true, 0x0000, 240, font_avr_classics);

    int y = 48;
    for (int i = 0; i < MODE_COUNT; i++) {
        uint16_t col = (i == sel) ? 0xFFFF : 0x7BEF;
        char line[40];
        snprintf(line, sizeof(line), "%s %s", (i == sel) ? "[]" : "  ", modes[i].title);
        fb_draw_text(0, 4, y, line, col, 2,
                     0, true, 0x0000, 240, font_avr_classics);
        y += 24;
    }

    // Bottom half: description of selected only.
    fb_draw_text(0, 4, 180, modes[sel].expl, 0x001F, 1,
                 0, true, 0x0000, 230, font_avr_classics);

    fb_draw_text(0, 4, 260, "scroll | click   or /bootseq", 0x7BEF, 1,
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

boot_role_t boot_mode_select_if_held(void)
{
    bool held = boot_menu_button_held();

    // Always announce the serial rescue channel.
    serial_write("\r\n[early] /bootseq help   (serial rescue)\r\n");

    if (!held) {
        // Short listen window so a stuck unit can still be rescued.
        char line[96];
        int pos = 0;
        TickType_t until = xTaskGetTickCount() + pdMS_TO_TICKS(SERIAL_RESCUE_MS);
        boot_role_t chosen = BOOT_ROLE_SOLO;
        while (xTaskGetTickCount() < until) {
            if (serial_read_line(line, sizeof(line), &pos)) {
                if (boot_serial_handle_line(line, &chosen)) {
                    return chosen;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
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
    char line[96];
    int pos = 0;
    boot_role_t serial_role = BOOT_ROLE_SOLO;

    draw_mode_screen(sel, seconds);
    serial_write("mode select active — /bootseq setconfvar mode <name>\r\n");

    while (seconds > 0) {
        if (enc) ky040_poll(enc);

        if (s_enc_delta) {
            sel = (sel + s_enc_delta + MODE_COUNT) % MODE_COUNT;
            s_enc_delta = 0;
            draw_mode_screen(sel, seconds);
        }

        if (serial_read_line(line, sizeof(line), &pos)) {
            if (boot_serial_handle_line(line, &serial_role)) {
                return serial_role;
            }
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
            serial_write("shutting down\r\n");
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
            serial_write("firmware flash: connect USB or another unit\r\n");
            commit_role_nvs(BOOT_ROLE_SOLO);
            return BOOT_ROLE_SOLO;
        default:
            commit_role_nvs(modes[chosen].role);
            return modes[chosen].role;
    }
    return BOOT_ROLE_SOLO;
}
