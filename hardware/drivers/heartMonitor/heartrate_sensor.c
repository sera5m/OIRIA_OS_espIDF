#include "max30102_sensor.h"
#include "hr_metrics.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "MAX30102";

static esp_err_t wr(max30102_dev_t *dev, uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(dev->i2c, b, 2, 50);
}

static esp_err_t rd(max30102_dev_t *dev, uint8_t reg, uint8_t *out, size_t n)
{
    return i2c_master_transmit_receive(dev->i2c, &reg, 1, out, n, 50);
}

const char *max30102_state_str(max30102_state_t s)
{
    switch (s) {
        case MAX30102_STATE_SLEEP: return "sleep";
        case MAX30102_STATE_HR:    return "hr";
        case MAX30102_STATE_SPO2:  return "spo2";
        default:                   return "uninit";
    }
}

esp_err_t max30102_init(max30102_dev_t *dev,
                        i2c_master_bus_handle_t bus,
                        gpio_num_t int_pin)
{
    if (!dev || !bus) return ESP_ERR_INVALID_ARG;
    memset(dev, 0, sizeof(*dev));
    dev->int_pin = int_pin;
    dev->state = MAX30102_STATE_UNINIT;
    dev->sample_hz = 50;

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX30102_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &dev->i2c);
    if (err != ESP_OK) return err;

    uint8_t part = 0;
    err = rd(dev, MAX30102_REG_PART_ID, &part, 1);
    if (err != ESP_OK || part != MAX30102_PART_ID) {
        ESP_LOGE(TAG, "part id 0x%02x (want 0x15)", part);
        return ESP_ERR_NOT_FOUND;
    }

    // Reset, then leave in shutdown until set_mode.
    wr(dev, MAX30102_REG_MODE_CONFIG, 0x40);
    vTaskDelay(pdMS_TO_TICKS(10));
    wr(dev, MAX30102_REG_MODE_CONFIG, 0x80);   // SHDN
    dev->state = MAX30102_STATE_SLEEP;
    ESP_LOGI(TAG, "MAX30102 up, part=0x%02x", part);
    return ESP_OK;
}

esp_err_t max30102_shutdown(max30102_dev_t *dev)
{
    if (!dev || !dev->i2c) return ESP_ERR_INVALID_STATE;
    esp_err_t err = wr(dev, MAX30102_REG_MODE_CONFIG, 0x80);
    if (err == ESP_OK) dev->state = MAX30102_STATE_SLEEP;
    return err;
}

esp_err_t max30102_set_mode(max30102_dev_t *dev, max30102_state_t mode)
{
    if (!dev || !dev->i2c) return ESP_ERR_INVALID_STATE;

    if (mode == MAX30102_STATE_SLEEP) return max30102_shutdown(dev);

    // FIFO: sample avg = 2 (01), rollover on, A_FULL = 15.
    // Effective rate 100/2 = 50 Hz.
    wr(dev, MAX30102_REG_FIFO_CONFIG, (1 << 5) | (1 << 4) | 0x0F);
    // SpO2 config: ADC 4096, 100 Hz, pulse 411 us.
    wr(dev, MAX30102_REG_SPO2_CONFIG, (1 << 5) | (1 << 2) | 0x03);
    wr(dev, MAX30102_REG_LED1_PA, 0x24);
    wr(dev, MAX30102_REG_LED2_PA, 0x24);
    wr(dev, MAX30102_REG_FIFO_WR, 0);
    wr(dev, MAX30102_REG_OVF_CTR, 0);
    wr(dev, MAX30102_REG_FIFO_RD, 0);
    wr(dev, MAX30102_REG_INT_ENABLE1, 0x40);   // PPG_RDY

    uint8_t mode_reg = (mode == MAX30102_STATE_HR) ? MAX30102_MODE_HR
                                                    : MAX30102_MODE_SPO2;
    esp_err_t err = wr(dev, MAX30102_REG_MODE_CONFIG, mode_reg);
    if (err != ESP_OK) return err;

    dev->state = mode;
    dev->win_n = 0;
    dev->sample_hz = 50;
    ESP_LOGI(TAG, "mode %s", max30102_state_str(mode));
    return ESP_OK;
}

// Drain FIFO. SpO2 mode is 6 bytes/sample, HR-only is 3.
static esp_err_t drain_fifo(max30102_dev_t *dev)
{
    uint8_t wr_ptr = 0, rd_ptr = 0;
    rd(dev, MAX30102_REG_FIFO_WR, &wr_ptr, 1);
    rd(dev, MAX30102_REG_FIFO_RD, &rd_ptr, 1);
    int n = (wr_ptr - rd_ptr) & 0x1F;
    if (n <= 0) return ESP_OK;

    bool spo2 = (dev->state == MAX30102_STATE_SPO2);
    int stride = spo2 ? 6 : 3;

    for (int i = 0; i < n; i++) {
        uint8_t buf[6] = {0};
        if (rd(dev, MAX30102_REG_FIFO_DATA, buf, stride) != ESP_OK) break;

        uint32_t red = 0, ir = 0;
        if (spo2) {
            red = ((uint32_t)buf[0] << 16 | (uint32_t)buf[1] << 8 | buf[2]) & 0x3FFFF;
            ir  = ((uint32_t)buf[3] << 16 | (uint32_t)buf[4] << 8 | buf[5]) & 0x3FFFF;
        } else {
            ir = ((uint32_t)buf[0] << 16 | (uint32_t)buf[1] << 8 | buf[2]) & 0x3FFFF;
        }

        if (dev->win_n < MAX30102_WIN) {
            dev->ir[dev->win_n] = ir;
            dev->red[dev->win_n] = red;
            dev->win_n++;
        } else {
            memmove(dev->ir, dev->ir + 1, (MAX30102_WIN - 1) * sizeof(uint32_t));
            memmove(dev->red, dev->red + 1, (MAX30102_WIN - 1) * sizeof(uint32_t));
            dev->ir[MAX30102_WIN - 1] = ir;
            dev->red[MAX30102_WIN - 1] = red;
        }
    }
    return ESP_OK;
}

// Maxim PBA (peripheral beat amplitude), adapted from the SparkFun
// heartRate.cpp port of Maxim's algorithm.
// Copyright (C) 2016 Maxim Integrated Products, Inc.
// Permission granted to use, copy, modify; see SparkFun MAX3010x library.
// Not for medical diagnosis.

static const int16_t k_fir[12] = {
    172, 321, 579, 927, 1360, 1858, 2390, 2916, 3391, 3768, 4012, 4096
};

static int16_t pba_dc(int32_t *reg, uint32_t x)
{
    *reg += ((((int32_t)x << 15) - *reg) >> 4);
    return (int16_t)(*reg >> 15);
}

static int16_t pba_fir(max30102_dev_t *dev, int16_t din)
{
    uint8_t off = dev->pba.fir_off;
    dev->pba.fir[off] = din;
    int32_t z = (int32_t)k_fir[11] * dev->pba.fir[(off - 11) & 31];
    for (int i = 0; i < 11; i++) {
        int16_t a = dev->pba.fir[(off - i) & 31];
        int16_t b = dev->pba.fir[(off - 22 + i) & 31];
        z += (int32_t)k_fir[i] * (a + b);
    }
    dev->pba.fir_off = (off + 1) & 31;
    return (int16_t)(z >> 15);
}

// One IR sample in. Returns true on a detected beat.
static bool pba_sample(max30102_dev_t *dev, uint32_t sample)
{
    dev->pba.ac_prev = dev->pba.ac_cur;
    int16_t dc = pba_dc(&dev->pba.dc_reg, sample);
    dev->pba.ac_cur = pba_fir(dev, (int16_t)((int32_t)sample - dc));

    bool beat = false;
    int16_t prev = dev->pba.ac_prev;
    int16_t cur  = dev->pba.ac_cur;

    if (prev < 0 && cur >= 0) {
        dev->pba.ac_max_hold = dev->pba.ac_max;
        dev->pba.ac_min_hold = dev->pba.ac_min;
        dev->pba.pos_edge = 1;
        dev->pba.neg_edge = 0;
        dev->pba.ac_max = 0;
        int amp = (int)dev->pba.ac_max_hold - (int)dev->pba.ac_min_hold;
        if (amp > 20 && amp < 1000) beat = true;
    }
    if (prev > 0 && cur <= 0) {
        dev->pba.pos_edge = 0;
        dev->pba.neg_edge = 1;
        dev->pba.ac_min = 0;
    }
    if (dev->pba.pos_edge && cur > prev) dev->pba.ac_max = cur;
    if (dev->pba.neg_edge && cur < prev) dev->pba.ac_min = cur;
    return beat;
}

// Ratio-of-ratios over the current window, then Maxim's published quadratic
// (algorithm.cpp comment): SpO2 = -45.060 R^2 + 30.354 R + 94.845.
// Still an estimate; calibration depends on the optical stack.
static void compute_spo2(max30102_dev_t *dev)
{
    if (dev->state != MAX30102_STATE_SPO2 || dev->win_n < 32) return;

    uint64_t irdc = 0, reddc = 0;
    for (uint16_t i = 0; i < dev->win_n; i++) {
        irdc += dev->ir[i];
        reddc += dev->red[i];
    }
    irdc /= dev->win_n;
    reddc /= dev->win_n;
    if (irdc < 5000 || reddc < 5000) return;

    uint32_t irac = 0, redac = 0;
    for (uint16_t i = 0; i < dev->win_n; i++) {
        int32_t d = (int32_t)dev->ir[i] - (int32_t)irdc;
        if (d < 0) d = -d;
        if ((uint32_t)d > irac) irac = d;
        d = (int32_t)dev->red[i] - (int32_t)reddc;
        if (d < 0) d = -d;
        if ((uint32_t)d > redac) redac = d;
    }
    if (irac == 0 || redac == 0) return;

    float R = ((float)redac / (float)reddc) / ((float)irac / (float)irdc);
    float spo2 = (-45.060f * R + 30.354f) * R + 94.845f;
    if (spo2 < 70.0f) spo2 = 70.0f;
    if (spo2 > 100.0f) spo2 = 100.0f;
    dev->spo2 = (uint8_t)(spo2 + 0.5f);
}

esp_err_t max30102_update(max30102_dev_t *dev)
{
    if (!dev || dev->state == MAX30102_STATE_UNINIT) return ESP_ERR_INVALID_STATE;
    if (dev->state == MAX30102_STATE_SLEEP) return ESP_ERR_INVALID_STATE;

    esp_err_t err = drain_fifo(dev);
    if (err != ESP_OK) return err;

    // Run PBA on samples that arrived since the last update.
    // We only have the window, so walk the newest stretch.
    static uint16_t s_seen;
    uint16_t start = (dev->win_n > 8) ? (uint16_t)(dev->win_n - 8) : 0;
    if (dev->win_n < s_seen) s_seen = 0;
    if (start < s_seen) start = s_seen;

    bool beat = false;
    for (uint16_t i = start; i < dev->win_n; i++) {
        if (pba_sample(dev, dev->ir[i])) beat = true;
    }
    s_seen = dev->win_n;

    if (beat) {
        int64_t now = esp_timer_get_time();
        if (dev->pba.last_beat_us) {
            int64_t dt = now - dev->pba.last_beat_us;
            if (dt > 300000 && dt < 2000000) {          // 30..200 bpm
                int bpm = (int)(60000000LL / dt);
                dev->bpm = (uint8_t)bpm;
                dev->beat = true;
                compute_spo2(dev);
                hr_metrics_push(dev->bpm, dev->spo2);
            }
        }
        dev->pba.last_beat_us = now;
    }
    return ESP_OK;
}

esp_err_t max30102_read_temperature(max30102_dev_t *dev, float *temp_c)
{
    if (!dev || !temp_c) return ESP_ERR_INVALID_ARG;
    wr(dev, MAX30102_REG_TEMP_CONFIG, 0x01);
    vTaskDelay(pdMS_TO_TICKS(30));
    uint8_t ti = 0, tf = 0;
    rd(dev, MAX30102_REG_TEMP_INT, &ti, 1);
    rd(dev, MAX30102_REG_TEMP_FRAC, &tf, 1);
    *temp_c = (int8_t)ti + tf * 0.0625f;
    return ESP_OK;
}
