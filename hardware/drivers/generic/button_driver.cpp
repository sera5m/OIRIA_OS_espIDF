#include "button_driver.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>

#define TAG "BUTTON"

struct button_impl_t {
    gpio_num_t pin;
    bool active_low;
    uint32_t debounce_ms;
    button_cb_t on_press;
    button_cb_t on_release;
    void *user_ctx;

    // state
    bool last_stable_state;   // debounced state (true = pressed)
    bool last_raw_state;
    uint32_t last_change_time_ms;
};

esp_err_t button_new(const button_config_t *cfg, button_handle_t *out_handle) {
    if (!cfg || cfg->pin == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }

    button_handle_t self = (button_handle_t)calloc(1, sizeof(*self));
    if (!self) return ESP_ERR_NO_MEM;

    self->pin = cfg->pin;
    self->active_low = cfg->active_low;
    self->debounce_ms = cfg->debounce_ms ? cfg->debounce_ms : 50;
    self->on_press = cfg->on_press;
    self->on_release = cfg->on_release;
    self->user_ctx = cfg->user_ctx;

    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << self->pin),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    if (self->active_low) {
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    } else {
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;   // ✅ Use PULLDOWN_ENABLE, not PULLUP_ENABLE
    }


    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        free(self);
        return ret;
    }

    // Read initial state
    int raw = gpio_get_level(self->pin);
    self->last_raw_state = (raw == (self->active_low ? 0 : 1));
    self->last_stable_state = self->last_raw_state;
    self->last_change_time_ms = esp_timer_get_time() / 1000;

    *out_handle = self;
    ESP_LOGI(TAG, "Button init OK: pin=%d, active_low=%d, debounce=%d ms",
             self->pin, self->active_low, self->debounce_ms);
    return ESP_OK;
}

esp_err_t button_del(button_handle_t handle) {
    if (!handle) return ESP_OK;
    free(handle);
    return ESP_OK;
}

void button_poll(button_handle_t self) {
    if (!self) return;

    int raw = gpio_get_level(self->pin);
    bool raw_state = (raw == (self->active_low ? 0 : 1));
    uint32_t now_ms = esp_timer_get_time() / 1000;

    // If raw state changed, update timer
    if (raw_state != self->last_raw_state) {
        self->last_raw_state = raw_state;
        self->last_change_time_ms = now_ms;
        return;
    }

    // No change, check if debounce time elapsed
    if (now_ms - self->last_change_time_ms < self->debounce_ms) {
        return;
    }

    // Stable for debounce period
    if (raw_state == self->last_stable_state) {
        return; // no change
    }

    // State changed after debounce
    self->last_stable_state = raw_state;

    if (raw_state) {
        // Pressed
        if (self->on_press) {
            self->on_press(self->user_ctx, true);
        }
    } else {
        // Released
        if (self->on_release) {
            self->on_release(self->user_ctx, false);
        }
    }
}

bool button_is_pressed(button_handle_t self) {
    if (!self) return false;
    return self->last_stable_state;
}