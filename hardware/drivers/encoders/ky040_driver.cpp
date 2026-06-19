#include "hardware/drivers/encoders/ky040_driver.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>

#define TAG "KY040"

struct ky040_impl_t {
    pcnt_unit_handle_t pcnt_unit;
    int detents_per_rev;
    ky040_twist_cb_t on_twist;
    void *user_ctx;

    int last_count;
    int64_t last_event_us;
    int recent_deltas_sum;
    int delta_count;

    // Hysteresis + timeout
    int pending_steps;
    int last_direction;
    int64_t last_step_us;

    // Interrupt support
    ky040_isr_cb_t on_pcnt_isr;
    int watch_high_value;
    int watch_low_value;
};

static bool pcnt_watchpoint_cb(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx) {
    ky040_handle_t self = (ky040_handle_t)user_ctx;
    if (self && self->on_pcnt_isr) {
        self->on_pcnt_isr(self->user_ctx);
    }
    return true;
}

// ====================== Watchpoint API ======================
esp_err_t ky040_enable_watchpoint(ky040_handle_t self, int threshold, ky040_isr_cb_t cb) {
    if (!self || !self->pcnt_unit) return ESP_ERR_INVALID_STATE;
    
    self->on_pcnt_isr = cb;
    self->watch_high_value = threshold;
    self->watch_low_value  = -threshold;

    esp_err_t ret = pcnt_unit_add_watch_point(self->pcnt_unit, self->watch_high_value);
    if (ret != ESP_OK) return ret;
    ret = pcnt_unit_add_watch_point(self->pcnt_unit, self->watch_low_value);
    if (ret != ESP_OK) return ret;

    pcnt_event_callbacks_t cbs = { .on_reach = pcnt_watchpoint_cb };
    return pcnt_unit_register_event_callbacks(self->pcnt_unit, &cbs, self);
}

esp_err_t ky040_disable_watchpoint(ky040_handle_t self) {
    if (!self || !self->pcnt_unit) return ESP_ERR_INVALID_STATE;

    if (self->watch_high_value) {
        pcnt_unit_remove_watch_point(self->pcnt_unit, self->watch_high_value);
        self->watch_high_value = 0;
    }
    if (self->watch_low_value) {
        pcnt_unit_remove_watch_point(self->pcnt_unit, self->watch_low_value);
        self->watch_low_value = 0;
    }

    pcnt_event_callbacks_t cbs = {0};
    pcnt_unit_register_event_callbacks(self->pcnt_unit, &cbs, NULL);
    self->on_pcnt_isr = NULL;
    return ESP_OK;
}

// ====================== Init / Cleanup ======================
esp_err_t ky040_new(const ky040_config_t *cfg, ky040_handle_t *out_handle) {
    *out_handle = NULL;
    if (!cfg || cfg->clk_pin == KY040_PIN_UNUSED || cfg->dt_pin == KY040_PIN_UNUSED) {
        ESP_LOGE(TAG, "Invalid config: missing CLK or DT pin");
        return ESP_ERR_INVALID_ARG;
    }

    ky040_handle_t self = (ky040_handle_t)calloc(1, sizeof(*self));
    if (!self) return ESP_ERR_NO_MEM;

    self->detents_per_rev = cfg->detents_per_rev ? cfg->detents_per_rev : KY040_DEFAULT_DETENTS_PER_REV;
    self->on_twist        = cfg->on_twist;
    self->user_ctx        = cfg->user_ctx;
    self->watch_high_value = 0;
    self->watch_low_value  = 0;

    // PCNT setup (same as before)
    pcnt_unit_config_t unit_cfg = {.low_limit = -32768, .high_limit = 32767};
    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num  = cfg->clk_pin,
        .level_gpio_num = cfg->dt_pin,
    };
    pcnt_glitch_filter_config_t filter_cfg = {.max_glitch_ns = 5000};

    pcnt_channel_handle_t chan = NULL;
    esp_err_t ret;

    ret = pcnt_new_unit(&unit_cfg, &self->pcnt_unit); if (ret != ESP_OK) goto cleanup;
    ret = pcnt_new_channel(self->pcnt_unit, &chan_cfg, &chan); if (ret != ESP_OK) goto cleanup;
    ret = pcnt_channel_set_edge_action(chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE); if (ret != ESP_OK) goto cleanup;
    ret = pcnt_channel_set_level_action(chan, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE); if (ret != ESP_OK) goto cleanup;
    ret = pcnt_unit_set_glitch_filter(self->pcnt_unit, &filter_cfg); if (ret != ESP_OK) goto cleanup;

    ret = pcnt_unit_enable(self->pcnt_unit); if (ret != ESP_OK) goto cleanup;
    ret = pcnt_unit_clear_count(self->pcnt_unit); if (ret != ESP_OK) goto cleanup;
    ret = pcnt_unit_start(self->pcnt_unit); if (ret != ESP_OK) goto cleanup;

    if (cfg->use_interrupt && cfg->on_pcnt_interrupt) {
        ret = ky040_enable_watchpoint(self, 1, cfg->on_pcnt_interrupt);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Watchpoint failed, falling back to poll mode");
        }
    }

    *out_handle = self;
    ESP_LOGI(TAG, "KY-040 OK: CLK=%d DT=%d detents=%d mode=%s", 
             cfg->clk_pin, cfg->dt_pin, self->detents_per_rev,
             cfg->use_interrupt ? "INTERRUPT" : "POLL");
    return ESP_OK;

cleanup:
    if (self->pcnt_unit) {
        pcnt_unit_stop(self->pcnt_unit);
        pcnt_del_unit(self->pcnt_unit);
    }
    free(self);
    return ret;
}

esp_err_t ky040_del(ky040_handle_t self) {
    if (!self) return ESP_OK;
    ky040_disable_watchpoint(self);
    if (self->pcnt_unit) {
        pcnt_unit_stop(self->pcnt_unit);
        pcnt_del_unit(self->pcnt_unit);
    }
    free(self);
    return ESP_OK;
}

// ====================== Core Poll with Old Hysteresis + Timeout ======================
void ky040_poll(ky040_handle_t self) {
    if (!self || !self->pcnt_unit) return;

    int count = 0;
    if (pcnt_unit_get_count(self->pcnt_unit, &count) != ESP_OK) return;

    int delta_total = count - self->last_count;
    if (delta_total == 0) {
        // Timeout check
        if (self->pending_steps != 0) {
            int64_t now = esp_timer_get_time();
            if (now - self->last_step_us > ABANDON_TIMEOUT_US) {
                ESP_LOGD(TAG, "Abandoning pending %d steps (timeout)", self->pending_steps);
                self->pending_steps = 0;
            }
        }
        return;
    }

    ESP_LOGI(TAG, "Raw delta_total = %+d (count=%d)", delta_total, count);

    self->last_count = count;
    int64_t now_us = esp_timer_get_time();
    self->last_step_us = now_us;

    int raw_steps = delta_total;
    int this_sign = (raw_steps > 0) ? 1 : (raw_steps < 0 ? -1 : 0);
    int abs_steps = raw_steps < 0 ? -raw_steps : raw_steps;

    if (abs_steps == 0) return;

    // === Old reliable hysteresis logic ===
    if (self->pending_steps == 0) {
        self->pending_steps = abs_steps;
        self->last_direction = this_sign;
        ESP_LOGD(TAG, "Started accumulating %d steps, dir=%d", abs_steps, this_sign);
    }
    else if (this_sign == self->last_direction) {
        self->pending_steps += abs_steps;
        ESP_LOGD(TAG, "Added %d steps (same dir), pending=%d", abs_steps, self->pending_steps);
    }
    else {
        self->pending_steps = abs_steps;
        self->last_direction = this_sign;
        ESP_LOGD(TAG, "Direction reversed → reset to %d steps", abs_steps);
    }

    if (self->pending_steps >= STEP_THRESHOLD) {
        int full_clicks = self->pending_steps / STEP_THRESHOLD;

        if (self->on_twist) {
            for (int i = 0; i < full_clicks; i++) {
                self->on_twist(self->user_ctx, self->last_direction);
            }
        }

        self->pending_steps %= STEP_THRESHOLD;
        ESP_LOGD(TAG, "Fired %d clicks dir=%d, %d left", full_clicks, self->last_direction, self->pending_steps);
    }
}
//temp set to 0
float ky040_get_rate_detents_per_sec(ky040_handle_t self) {return 0.0f; /* unchanged */ }
float ky040_get_rate_deg_per_sec(ky040_handle_t self) { return 0.0f;/* unchanged */ }