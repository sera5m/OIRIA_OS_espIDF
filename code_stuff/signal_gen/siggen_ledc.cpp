#include "siggen_ledc.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char* TAG = "siggen_ledc";

#ifndef SIGGEN_LEDC_SPEED
#define SIGGEN_LEDC_SPEED LEDC_LOW_SPEED_MODE
#endif
#ifndef SIGGEN_LEDC_TIMER
#define SIGGEN_LEDC_TIMER LEDC_TIMER_0
#endif
#ifndef SIGGEN_LEDC_CHANNEL
#define SIGGEN_LEDC_CHANNEL LEDC_CHANNEL_0
#endif
#ifndef SIGGEN_LEDC_RES
#define SIGGEN_LEDC_RES LEDC_TIMER_10_BIT
#endif

esp_err_t siggen_ledc_init(siggen_state_t* st) {
    if (!st) return ESP_ERR_INVALID_ARG;
    st->ledc_timer = SIGGEN_LEDC_TIMER;
    st->ledc_channel = SIGGEN_LEDC_CHANNEL;

    uint32_t freq = st->cfg.freq_hz ? st->cfg.freq_hz : 1000;
    ledc_timer_config_t tcfg = {
        .speed_mode = SIGGEN_LEDC_SPEED,
        .duty_resolution = SIGGEN_LEDC_RES,
        .timer_num = (ledc_timer_t)st->ledc_timer,
        .freq_hz = freq,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t e = ledc_timer_config(&tcfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "timer_config: %s", esp_err_to_name(e));
        return e;
    }

    uint32_t max_duty = (1u << 10) - 1;
    uint32_t duty = (st->cfg.duty_percent * max_duty) / 100;

    ledc_channel_config_t ccfg = {
        .gpio_num = st->cfg.gpio,
        .speed_mode = SIGGEN_LEDC_SPEED,
        .channel = (ledc_channel_t)st->ledc_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = (ledc_timer_t)st->ledc_timer,
        .duty = duty,
        .hpoint = 0,
    };
    e = ledc_channel_config(&ccfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "channel_config: %s", esp_err_to_name(e));
        return e;
    }
    st->inited = true;
    ESP_LOGI(TAG, "LEDC gpio=%d freq=%lu duty=%u%%", st->cfg.gpio,
             (unsigned long)freq, st->cfg.duty_percent);
    return ESP_OK;
}

esp_err_t siggen_ledc_start(siggen_state_t* st) {
    if (!st || !st->inited) return ESP_ERR_INVALID_STATE;
    st->cfg.running = true;
    return ESP_OK;
}

esp_err_t siggen_ledc_stop(siggen_state_t* st) {
    if (!st || !st->inited) return ESP_ERR_INVALID_STATE;
    ledc_stop(SIGGEN_LEDC_SPEED, (ledc_channel_t)st->ledc_channel, 0);
    st->cfg.running = false;
    return ESP_OK;
}

esp_err_t siggen_ledc_set_freq(siggen_state_t* st, uint32_t freq_hz) {
    if (!st || !st->inited) return ESP_ERR_INVALID_STATE;
    st->cfg.freq_hz = freq_hz;
    return ledc_set_freq(SIGGEN_LEDC_SPEED, (ledc_timer_t)st->ledc_timer, freq_hz);
}

esp_err_t siggen_ledc_set_duty(siggen_state_t* st, uint8_t duty_percent) {
    if (!st || !st->inited) return ESP_ERR_INVALID_STATE;
    if (duty_percent > 100) duty_percent = 100;
    st->cfg.duty_percent = duty_percent;
    uint32_t max_duty = (1u << 10) - 1;
    uint32_t duty = (duty_percent * max_duty) / 100;
    esp_err_t e = ledc_set_duty(SIGGEN_LEDC_SPEED, (ledc_channel_t)st->ledc_channel, duty);
    if (e != ESP_OK) return e;
    return ledc_update_duty(SIGGEN_LEDC_SPEED, (ledc_channel_t)st->ledc_channel);
}

esp_err_t siggen_ledc_apply_sample(siggen_state_t* st, int16_t sample_q15) {
    if (!st || !st->inited) return ESP_ERR_INVALID_STATE;
    // map [-32768,32767] → duty 0..max
    uint32_t max_duty = (1u << 10) - 1;
    int32_t u = ((int32_t)sample_q15 + 32768);
    uint32_t duty = ((uint32_t)u * max_duty) / 65535u;
    esp_err_t e = ledc_set_duty(SIGGEN_LEDC_SPEED, (ledc_channel_t)st->ledc_channel, duty);
    if (e != ESP_OK) return e;
    return ledc_update_duty(SIGGEN_LEDC_SPEED, (ledc_channel_t)st->ledc_channel);
}
