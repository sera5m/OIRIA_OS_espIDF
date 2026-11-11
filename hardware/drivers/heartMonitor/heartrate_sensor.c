#include "max30102_sensor.h"
#include "hr_metrics.h"
#include "esp_log.h"
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

// Peak interval on IR. DC is the window mean; a peak is a local max
// at least 1/64 of the mean above its neighbors.
static void compute_bpm(max30102_dev_t *dev)
{
    dev->beat = false;
    if (dev->win_n < dev->sample_hz) return;   // need ~1 s

    uint64_t sum = 0;
    for (uint16_t i = 0; i < dev->win_n; i++) sum += dev->ir[i];
    uint32_t dc = (uint32_t)(sum / dev->win_n);
    if (dc < 1000) return;                     // no finger

    uint32_t thresh = dc / 64;
    if (thresh < 20) thresh = 20;

    int peaks[16];
    int np = 0;
    int min_gap = dev->sample_hz * 60 / 200;   // 200 bpm max
    if (min_gap < 4) min_gap = 4;

    for (uint16_t i = 2; i + 2 < dev->win_n && np < 16; i++) {
        uint32_t v = dev->ir[i];
        if (v + thresh < dc) continue;
        if (v <= dev->ir[i - 1] || v < dev->ir[i + 1]) continue;
        if (v <= dev->ir[i - 2] || v < dev->ir[i + 2]) continue;
        if (np > 0 && (int)i - peaks[np - 1] < min_gap) continue;
        peaks[np++] = (int)i;
    }
    if (np < 2) return;

    int span = peaks[np - 1] - peaks[0];
    int intervals = np - 1;
    if (span <= 0) return;
    int bpm = (int)((int64_t)intervals * dev->sample_hz * 60 / span);
    if (bpm < 30 || bpm > 200) return;

    dev->bpm = (uint8_t)bpm;
    dev->beat = true;
}

// Ratio-of-ratios. Rough; good enough to see a trend, not a medical value.
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
    if (irdc < 1000 || reddc < 1000) return;

    uint32_t irac = 0, redac = 0;
    for (uint16_t i = 0; i < dev->win_n; i++) {
        int32_t d = (int32_t)dev->ir[i] - (int32_t)irdc;
        if (d < 0) d = -d;
        if ((uint32_t)d > irac) irac = (uint32_t)d;
        d = (int32_t)dev->red[i] - (int32_t)reddc;
        if (d < 0) d = -d;
        if ((uint32_t)d > redac) redac = (uint32_t)d;
    }
    if (irac == 0) return;

    // R = (red_ac/red_dc) / (ir_ac/ir_dc). Linear map, clamped.
    float R = ((float)redac / (float)reddc) / ((float)irac / (float)irdc);
    int spo2 = (int)(110.0f - 25.0f * R);
    if (spo2 < 70) spo2 = 70;
    if (spo2 > 100) spo2 = 100;
    dev->spo2 = (uint8_t)spo2;
}

esp_err_t max30102_update(max30102_dev_t *dev)
{
    if (!dev || dev->state == MAX30102_STATE_UNINIT) return ESP_ERR_INVALID_STATE;
    if (dev->state == MAX30102_STATE_SLEEP) return ESP_ERR_INVALID_STATE;

    esp_err_t err = drain_fifo(dev);
    if (err != ESP_OK) return err;

    uint8_t prev = dev->bpm;
    compute_bpm(dev);
    compute_spo2(dev);

    // Hand the short-period result to the logger. A change is what RLE keeps.
    if (dev->bpm && dev->bpm != prev) {
        hr_metrics_push(dev->bpm, dev->spo2);
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
