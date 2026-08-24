#include "siggen_i2s_pdm.h"
#include "siggen_wavetable.h"
#include "precomputed_math/unit_circle_i16.h"
#include "precomputed_math/fast_inv_trig_i16.h"

#include "driver/i2s_pdm.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "siggen_pdm";

esp_err_t siggen_i2s_pdm_init(siggen_state_t* st) {
    if (!st) return ESP_ERR_INVALID_ARG;

    uint32_t sr = st->cfg.sample_rate ? st->cfg.sample_rate : 48000;
    st->cfg.sample_rate = sr;
    st->phase = 0;
    st->phase_inc = pcm_phase_inc(st->cfg.freq_hz ? st->cfg.freq_hz : 1000, sr);

    i2s_chan_handle_t tx = nullptr;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t e = i2s_new_channel(&chan_cfg, &tx, nullptr);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(e));
        return e;
    }

    i2s_pdm_tx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(sr),
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = GPIO_NUM_NC,           // unused clock line if possible
            .dout = (gpio_num_t)st->cfg.gpio,
            .invert_flags = { false, false },
        },
    };
    e = i2s_channel_init_pdm_tx_mode(tx, &pdm_cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "init_pdm_tx: %s", esp_err_to_name(e));
        i2s_del_channel(tx);
        return e;
    }

    st->i2s_tx = (void*)tx;
    st->inited = true;
    ESP_LOGI(TAG, "PDM TX dout=GPIO%d sr=%lu freq=%lu", st->cfg.gpio,
             (unsigned long)sr, (unsigned long)st->cfg.freq_hz);
    return ESP_OK;
}

esp_err_t siggen_i2s_pdm_start(siggen_state_t* st) {
    if (!st || !st->inited || !st->i2s_tx) return ESP_ERR_INVALID_STATE;
    esp_err_t e = i2s_channel_enable((i2s_chan_handle_t)st->i2s_tx);
    if (e == ESP_OK) st->cfg.running = true;
    return e;
}

esp_err_t siggen_i2s_pdm_stop(siggen_state_t* st) {
    if (!st || !st->inited || !st->i2s_tx) return ESP_ERR_INVALID_STATE;
    i2s_channel_disable((i2s_chan_handle_t)st->i2s_tx);
    st->cfg.running = false;
    return ESP_OK;
}

esp_err_t siggen_i2s_pdm_write(siggen_state_t* st, const int16_t* samples, size_t n_samples) {
    if (!st || !st->i2s_tx || !samples) return ESP_ERR_INVALID_ARG;
    size_t written = 0;
    return i2s_channel_write((i2s_chan_handle_t)st->i2s_tx, samples,
                             n_samples * sizeof(int16_t), &written, portMAX_DELAY);
}

void siggen_i2s_pdm_task(void* arg) {
    siggen_state_t* st = (siggen_state_t*)arg;
    if (!st) { vTaskDelete(NULL); return; }

    const size_t N = 256;
    int16_t buf[N];
    int16_t amp = (int16_t)((st->cfg.amplitude > 100 ? 100 : st->cfg.amplitude) * 32767 / 100);

    while (st->cfg.running) {
        st->phase_inc = pcm_phase_inc(st->cfg.freq_hz, st->cfg.sample_rate);
        siggen_fill(st->cfg.wave, buf, N, &st->phase, st->phase_inc, amp, st->cfg.duty_percent);
        if (siggen_i2s_pdm_write(st, buf, N) != ESP_OK) {
            ESP_LOGW(TAG, "write failed — stopping task");
            break;
        }
    }
    vTaskDelete(NULL);
}
