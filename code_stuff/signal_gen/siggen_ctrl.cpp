#include "siggen_ctrl.h"
#include "siggen_ledc.h"
#include "siggen_i2s_pdm.h"
#include "precomputed_math/fast_inv_trig_i16.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "siggen";
static siggen_state_t g_st;
static TaskHandle_t g_pdm_task = nullptr;

siggen_state_t* siggen_get(void) { return &g_st; }

esp_err_t siggen_configure(const siggen_cfg_t* cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (g_st.cfg.running) siggen_stop();

    memset(&g_st, 0, sizeof(g_st));
    g_st.cfg = *cfg;
    if (g_st.cfg.sample_rate == 0) g_st.cfg.sample_rate = 48000;
    if (g_st.cfg.freq_hz == 0) g_st.cfg.freq_hz = 1000;
    if (g_st.cfg.amplitude == 0) g_st.cfg.amplitude = 80;
    g_st.phase_inc = pcm_phase_inc(g_st.cfg.freq_hz, g_st.cfg.sample_rate);
    g_st.ledc_channel = -1;

    esp_err_t e = ESP_OK;
    switch (g_st.cfg.out) {
        case SIGGEN_OUT_LEDC:
            e = siggen_ledc_init(&g_st);
            break;
        case SIGGEN_OUT_I2S_PDM:
            e = siggen_i2s_pdm_init(&g_st);
            break;
        case SIGGEN_OUT_GPIO:
            // Digital path: Vulcan host pin_mode + gpio_wr / RMT later
            g_st.inited = true;
            ESP_LOGI(TAG, "GPIO backend — drive via host gpio_wr / RMT");
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    return e;
}

esp_err_t siggen_start(void) {
    if (!g_st.inited) return ESP_ERR_INVALID_STATE;
    esp_err_t e = ESP_OK;
    switch (g_st.cfg.out) {
        case SIGGEN_OUT_LEDC:
            e = siggen_ledc_start(&g_st);
            // Square uses hardware freq; other waves need soft duty DDS task (future)
            break;
        case SIGGEN_OUT_I2S_PDM:
            e = siggen_i2s_pdm_start(&g_st);
            if (e == ESP_OK && !g_pdm_task) {
                xTaskCreate(siggen_i2s_pdm_task, "siggen_pdm", 4096, &g_st, 5, &g_pdm_task);
            }
            break;
        case SIGGEN_OUT_GPIO:
            g_st.cfg.running = true;
            break;
    }
    if (e == ESP_OK) ESP_LOGI(TAG, "started wave=%d out=%d f=%lu",
                              (int)g_st.cfg.wave, (int)g_st.cfg.out,
                              (unsigned long)g_st.cfg.freq_hz);
    return e;
}

esp_err_t siggen_stop(void) {
    if (!g_st.inited) return ESP_ERR_INVALID_STATE;
    g_st.cfg.running = false;
    switch (g_st.cfg.out) {
        case SIGGEN_OUT_LEDC:
            return siggen_ledc_stop(&g_st);
        case SIGGEN_OUT_I2S_PDM:
            if (g_pdm_task) {
                // task exits when running==false
                vTaskDelay(pdMS_TO_TICKS(20));
                g_pdm_task = nullptr;
            }
            return siggen_i2s_pdm_stop(&g_st);
        case SIGGEN_OUT_GPIO:
            return ESP_OK;
    }
    return ESP_OK;
}

esp_err_t siggen_set_freq(uint32_t freq_hz) {
    g_st.cfg.freq_hz = freq_hz;
    g_st.phase_inc = pcm_phase_inc(freq_hz, g_st.cfg.sample_rate ? g_st.cfg.sample_rate : 48000);
    if (g_st.cfg.out == SIGGEN_OUT_LEDC && g_st.inited)
        return siggen_ledc_set_freq(&g_st, freq_hz);
    return ESP_OK;
}

esp_err_t siggen_set_wave(siggen_wave_t wave) {
    g_st.cfg.wave = wave;
    return ESP_OK;
}

esp_err_t siggen_set_duty(uint8_t duty_percent) {
    g_st.cfg.duty_percent = duty_percent;
    if (g_st.cfg.out == SIGGEN_OUT_LEDC && g_st.inited)
        return siggen_ledc_set_duty(&g_st, duty_percent);
    return ESP_OK;
}

const siggen_cfg_t* siggen_cfg_snapshot(void) {
    return &g_st.cfg;
}
