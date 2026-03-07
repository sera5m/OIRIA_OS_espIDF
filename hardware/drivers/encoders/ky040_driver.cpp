#include "hardware/drivers/encoders/ky040_driver.hpp"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include <stdlib.h>

#define TAG "KY040"

struct ky040_impl_t {
    pcnt_unit_handle_t      pcnt_unit;
    int                     detents_per_rev;
    ky040_twist_cb_t        on_twist;
    void                   *user_ctx;
    int                     last_count;
    int64_t                 last_event_us;
    int                     recent_deltas_sum;
    int                     delta_count;
    gpio_num_t              sw_pin;
};

esp_err_t ky040_new(const ky040_config_t *cfg, ky040_handle_t *out_handle) {
    *out_handle = NULL;
    if (!cfg || cfg->clk_pin == KY040_PIN_UNUSED || cfg->dt_pin == KY040_PIN_UNUSED) {
        ESP_LOGE(TAG, "Invalid config: missing CLK or DT pin");
        return ESP_ERR_INVALID_ARG;
    }

    ky040_handle_t self = (ky040_handle_t)calloc(1, sizeof(*self));
    if (!self) {
        ESP_LOGE(TAG, "calloc failed");
        return ESP_ERR_NO_MEM;
    }

    self->detents_per_rev   = cfg->detents_per_rev ? cfg->detents_per_rev : KY040_DEFAULT_DETENTS_PER_REV;
    self->on_twist          = cfg->on_twist;
    self->user_ctx          = cfg->user_ctx;
    self->sw_pin            = cfg->sw_pin;
    self->last_count        = 0;
    self->last_event_us     = 0;
    self->recent_deltas_sum = 0;
    self->delta_count       = 0;

    pcnt_unit_config_t unit_cfg{};
    unit_cfg.low_limit  = -32768;
    unit_cfg.high_limit = 32767;

    pcnt_chan_config_t chan_cfg{};
    chan_cfg.edge_gpio_num  = cfg->clk_pin;
    chan_cfg.level_gpio_num = cfg->dt_pin;

    pcnt_glitch_filter_config_t filter_cfg = {
        .max_glitch_ns = 1000,
    };

    pcnt_channel_handle_t chan = NULL;
    esp_err_t ret;

    ret = pcnt_new_unit(&unit_cfg, &self->pcnt_unit);
    if (ret != ESP_OK) goto cleanup;

    ret = pcnt_new_channel(self->pcnt_unit, &chan_cfg, &chan);
    if (ret != ESP_OK) goto cleanup;

    ret = pcnt_channel_set_edge_action(chan,
                                        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                        PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    if (ret != ESP_OK) goto cleanup;

    ret = pcnt_channel_set_level_action(chan,
                                         PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                         PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    if (ret != ESP_OK) goto cleanup;

    ret = pcnt_unit_set_glitch_filter(self->pcnt_unit, &filter_cfg);
    if (ret != ESP_OK) goto cleanup;

    ret = pcnt_unit_enable(self->pcnt_unit);
    if (ret != ESP_OK) goto cleanup;

    ret = pcnt_unit_clear_count(self->pcnt_unit);
    if (ret != ESP_OK) goto cleanup;

    ret = pcnt_unit_start(self->pcnt_unit);
    if (ret != ESP_OK) goto cleanup;

    if (self->sw_pin != KY040_PIN_UNUSED) {
        gpio_config_t io = {
            .pin_bit_mask = (1ULL << self->sw_pin),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io));
    }

    *out_handle = self;
    ESP_LOGI(TAG, "KY-040 init OK: CLK=%d DT=%d SW=%d detents=%d",
             cfg->clk_pin, cfg->dt_pin, cfg->sw_pin, self->detents_per_rev);
    return ESP_OK;

cleanup:
    if (self->pcnt_unit) {
        pcnt_unit_stop(self->pcnt_unit);
        pcnt_del_unit(self->pcnt_unit);  // ← fixed: no &
    }
    free(self);
    return ret;
}

esp_err_t ky040_del(ky040_handle_t self) {
    if (!self) return ESP_OK;

    if (self->pcnt_unit) {
        pcnt_unit_stop(self->pcnt_unit);
        pcnt_del_unit(self->pcnt_unit);  // ← fixed: no &
    }
    free(self);
    return ESP_OK;
}

void ky040_poll(ky040_handle_t self) {
    if (!self || !self->pcnt_unit) return;

    int count = 0;
    if (pcnt_unit_get_count(self->pcnt_unit, &count) != ESP_OK) {
        return;
    }

    int delta_total = count - self->last_count;
    if (delta_total == 0) return;

    self->last_count = count;

    // Normalize to ±1 per detent (most KY-040 give ±2 per click in quadrature mode)
    int detent_steps = delta_total / 2;
    int sign = (detent_steps > 0) ? 1 : (detent_steps < 0 ? -1 : 0);
    int clicks = (detent_steps < 0) ? -detent_steps : detent_steps;

    int64_t now_us = esp_timer_get_time();

    if (self->on_twist) {
        for (int i = 0; i < clicks; i++) {
            self->on_twist(self->user_ctx, sign);
        }
    }

    // Basic speed tracking (optional, but harmless)
    if (clicks > 0) {
        self->recent_deltas_sum += sign * clicks;
        self->delta_count += clicks;
        self->last_event_us = now_us;
    }

    // Reset speed estimator after ~0.5s inactivity
    if (now_us - self->last_event_us > 500000) {
        self->recent_deltas_sum = 0;
        self->delta_count = 0;
    }
}
float ky040_get_rate_detents_per_sec(ky040_handle_t self) {
    if (!self || self->delta_count == 0) return 0.0f;

    int64_t age_us = esp_timer_get_time() - self->last_event_us;
    if (age_us > 750000) return 0.0f;

    return (float)self->recent_deltas_sum * (1000000.0f / (float)age_us);
}

float ky040_get_rate_deg_per_sec(ky040_handle_t self) {
    if (!self) return 0.0f;
    return ky040_get_rate_detents_per_sec(self) * (360.0f / (float)self->detents_per_rev);
}

bool ky040_is_button_pressed(ky040_handle_t self) {
    if (!self || self->sw_pin == KY040_PIN_UNUSED) return false;
    return gpio_get_level(self->sw_pin) == 0;
}