/*
 * ky040_driver.c
 *
 *  Created on: Jan 12, 2026
 *      Author: dev
 */

#ifndef KY040_DRIVER_C
#define KY040_DRIVER_C

// encoder_hw.c
// PURPOSE: raw physical encoder + button input only
// NO semantic meaning, NO routing, NO key mapping

// encoder_input.c
#include "hardware/wiring/wiring.h" //my wiring
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_timer"
#include "esp_log.h"
#include <stdbool.h>
#include <stdint.h>

#define TAG "ENC_IO"

// ---------- CONFIG ----------
#define ENC_COUNT 2

#define ENC0_CLK ENCODER0_CLK_PIN
#define ENC0_DT  ENCODER0_DT_PIN
#define ENC0_SW  ENCODER0_SW_PIN

#define ENC1_CLK ENCODER1_CLK_PIN
#define ENC1_DT  ENCODER1_DT_PIN
#define ENC1_SW  ENCODER1_SW_PIN
//referencing pins from wiring.h


#define BTN_DEBOUNCE_MS 40
#define POLL_INTERVAL_MS 10

// ---------- EXTERNAL CALLBACKS ----------
extern void inphandler_encoder_delta(uint8_t encoder_id, int32_t delta);
extern void inphandler_button_event(uint8_t button_id, bool pressed);

// ---------- INTERNAL STATE ----------
typedef struct {
    pcnt_unit_handle_t unit;
    int16_t last_count;
} encoder_hw_t;

static encoder_hw_t encoders[ENC_COUNT];

static uint32_t last_btn_time[ENC_COUNT] = {0};
static bool last_btn_state[ENC_COUNT] = {false};

// ---------- TIME ----------
static inline uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---------- SETUP ----------
static void setup_encoder(
    uint8_t id,
    gpio_num_t clk,
    gpio_num_t dt
) {
    pcnt_unit_config_t unit_cfg = {
        .low_limit = INT16_MIN,
        .high_limit = INT16_MAX,
    };

    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num = clk,
        .level_gpio_num = dt,
    };

    pcnt_glitch_filter_config_t filter_cfg = {
        .max_glitch_ns = 1000,
    };

    pcnt_channel_handle_t chan;

    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &encoders[id].unit));
    ESP_ERROR_CHECK(pcnt_new_channel(encoders[id].unit, &chan_cfg, &chan));

    ESP_ERROR_CHECK(
        pcnt_channel_set_edge_action(
            chan,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE,
            PCNT_CHANNEL_EDGE_ACTION_DECREASE
        )
    );

    ESP_ERROR_CHECK(
        pcnt_channel_set_level_action(
            chan,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP,
            PCNT_CHANNEL_LEVEL_ACTION_INVERSE
        )
    );

    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(encoders[id].unit, &filter_cfg));
    ESP_ERROR_CHECK(pcnt_unit_enable(encoders[id].unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(encoders[id].unit));
    ESP_ERROR_CHECK(pcnt_unit_start(encoders[id].unit));

    encoders[id].last_count = 0;
}

static void setup_button(gpio_num_t pin) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

// ---------- PUBLIC INIT ----------
void encoder_input_init(void) {
    setup_encoder(0, ENC0_CLK, ENC0_DT);
    setup_encoder(1, ENC1_CLK, ENC1_DT);

    setup_button(ENC0_SW);
    setup_button(ENC1_SW);

    ESP_LOGI(TAG, "encoder input initialized");
}

// ---------- POLLING ----------
void encoder_input_poll(void) {
    static uint32_t last_poll = 0;
    uint32_t now = now_ms();

    if (now - last_poll < POLL_INTERVAL_MS) return;
    last_poll = now;

    // ---- ENCODERS ----
    for (uint8_t i = 0; i < ENC_COUNT; i++) {
        int16_t count;
        if (pcnt_unit_get_count(encoders[i].unit, &count) != ESP_OK)
            continue;

        int16_t delta = count - encoders[i].last_count;
        if (delta != 0) {
            encoders[i].last_count = count;
            inphandler_encoder_delta(i, delta);
        }
    }

    // ---- BUTTONS ----
    const gpio_num_t btn_pins[ENC_COUNT] = {ENC0_SW, ENC1_SW};

    for (uint8_t i = 0; i < ENC_COUNT; i++) {
        bool pressed = gpio_get_level(btn_pins[i]) == 0;

        if (pressed != last_btn_state[i] &&
            (now - last_btn_time[i]) > BTN_DEBOUNCE_MS) {

            last_btn_time[i] = now;
            last_btn_state[i] = pressed;
            inphandler_button_event(i, pressed);
        }
    }
}


#endif /* MAIN_HARDWARE_DRIVERS_KY_040_ROTENCODERS_DRIVER_KY040_DRIVER_C_ */
