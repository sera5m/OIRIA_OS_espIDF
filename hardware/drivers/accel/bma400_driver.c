#include "bma400_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "BMA400";

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------

static esp_err_t wr_reg(bma400_dev_t *dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(dev->i2c_dev, buf, sizeof(buf), -1);
}

static esp_err_t rd_reg(bma400_dev_t *dev, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(dev->i2c_dev, &reg, 1, val, 1, -1);
}

static esp_err_t rd_burst(bma400_dev_t *dev, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(dev->i2c_dev, &reg, 1, buf, len, -1);
}

// ---------------------------------------------------------------------------
// Scale lookup (datasheet: 12-bit, ±2/4/8/16 g → 1024/512/256/128 LSB/g)
// ---------------------------------------------------------------------------

static float scale_for_range(uint8_t range)
{
    switch (range) {
        case BMA400_RANGE_2G:  return 1.0f / 1024.0f;
        case BMA400_RANGE_4G:  return 1.0f / 512.0f;
        case BMA400_RANGE_8G:  return 1.0f / 256.0f;
        case BMA400_RANGE_16G: return 1.0f / 128.0f;
        default:               return 1.0f / 1024.0f;
    }
}

static int16_t sign_extend_12(uint16_t raw)
{
    raw &= 0x0FFF;
    if (raw & 0x0800) raw |= 0xF000;
    return (int16_t)raw;
}

// ---------------------------------------------------------------------------
// Probe
// ---------------------------------------------------------------------------

static esp_err_t probe_addr(i2c_master_bus_handle_t bus, uint8_t addr,
                            i2c_master_dev_handle_t *out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, out);
    if (err != ESP_OK) return err;

    uint8_t chipid = 0;
    uint8_t reg = BMA400_REG_CHIPID;
    err = i2c_master_transmit_receive(*out, &reg, 1, &chipid, 1, -1);
    if (err != ESP_OK || chipid != BMA400_CHIP_ID) {
        i2c_master_bus_rm_device(*out);
        *out = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t bma400_init(bma400_dev_t *dev,
                      i2c_master_bus_handle_t bus,
                      gpio_num_t int1,
                      gpio_num_t int2)
{
    if (!dev || !bus) return ESP_ERR_INVALID_ARG;

    memset(dev, 0, sizeof(*dev));
    dev->int1_pin = int1;
    dev->int2_pin = int2;
    dev->range    = BMA400_RANGE_2G;
    dev->odr      = BMA400_ODR_100HZ;
    dev->scale    = scale_for_range(dev->range);
    dev->state    = BMA400_STATE_UNINIT;

    // Probe 0x14 then 0x15
    esp_err_t err = probe_addr(bus, BMA400_I2C_ADDR_SDO_LOW, &dev->i2c_dev);
    if (err != ESP_OK) {
        err = probe_addr(bus, BMA400_I2C_ADDR_SDO_HIGH, &dev->i2c_dev);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CHIPID probe failed (0x14 / 0x15)");
        dev->state = BMA400_STATE_ERROR;
        return err;
    }

    ESP_LOGI(TAG, "BMA400 found");

    // Soft reset, then leave in sleep
    err = bma400_soft_reset(dev);
    if (err != ESP_OK) {
        dev->state = BMA400_STATE_ERROR;
        return err;
    }

    dev->state = BMA400_STATE_SLEEP;
    ESP_LOGI(TAG, "init ok, state=SLEEP");
    return ESP_OK;
}

esp_err_t bma400_soft_reset(bma400_dev_t *dev)
{
    if (!dev || !dev->i2c_dev) return ESP_ERR_INVALID_STATE;
    esp_err_t err = wr_reg(dev, BMA400_REG_CMD, BMA400_CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(5));   // datasheet: ~1.5 ms typical
    return err;
}

esp_err_t bma400_set_mode(bma400_dev_t *dev, bma400_state_t target)
{
    if (!dev || !dev->i2c_dev) return ESP_ERR_INVALID_STATE;
    if (target == BMA400_STATE_UNINIT || target == BMA400_STATE_ERROR)
        return ESP_ERR_INVALID_ARG;

    uint8_t power = BMA400_POWER_SLEEP;
    switch (target) {
        case BMA400_STATE_SLEEP:     power = BMA400_POWER_SLEEP; break;
        case BMA400_STATE_LOW_POWER: power = BMA400_POWER_LOW;   break;
        case BMA400_STATE_NORMAL:    power = BMA400_POWER_NORMAL; break;
        default: return ESP_ERR_INVALID_ARG;
    }

    // When entering normal, make sure CONFIG1 (range/ODR/OSR) is written first
    if (target == BMA400_STATE_NORMAL) {
        uint8_t cfg1 = (dev->range << 6) | (0x00 << 4) | (dev->odr & 0x0F);
        esp_err_t e = wr_reg(dev, BMA400_REG_ACC_CONFIG1, cfg1);
        if (e != ESP_OK) return e;
    }

    // ACC_CONFIG0: filt1_bw=0, osr_lp=0, power_mode
    uint8_t cfg0 = power & 0x03;
    esp_err_t err = wr_reg(dev, BMA400_REG_ACC_CONFIG0, cfg0);
    if (err != ESP_OK) {
        dev->state = BMA400_STATE_ERROR;
        return err;
    }

    // Give the sensor a moment to settle
    vTaskDelay(pdMS_TO_TICKS(2));
    dev->state = target;
    ESP_LOGI(TAG, "mode -> %s", bma400_state_str(target));
    return ESP_OK;
}

esp_err_t bma400_configure(bma400_dev_t *dev, uint8_t range, uint8_t odr)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    if (range > BMA400_RANGE_16G) return ESP_ERR_INVALID_ARG;
    if (odr < BMA400_ODR_12_5HZ || odr > BMA400_ODR_800HZ) return ESP_ERR_INVALID_ARG;

    dev->range = range;
    dev->odr   = odr;
    dev->scale = scale_for_range(range);

    // If already running in normal, push the new config immediately
    if (dev->state == BMA400_STATE_NORMAL) {
        return bma400_set_mode(dev, BMA400_STATE_NORMAL);
    }
    return ESP_OK;
}

esp_err_t bma400_read_raw(bma400_dev_t *dev, bma400_raw_t *out)
{
    if (!dev || !out || !dev->i2c_dev) return ESP_ERR_INVALID_ARG;
    if (dev->state != BMA400_STATE_NORMAL &&
        dev->state != BMA400_STATE_LOW_POWER) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t buf[6];
    esp_err_t err = rd_burst(dev, BMA400_REG_ACC_X_LSB, buf, sizeof(buf));
    if (err != ESP_OK) return err;

    out->x = sign_extend_12((buf[1] << 8) | buf[0]);
    out->y = sign_extend_12((buf[3] << 8) | buf[2]);
    out->z = sign_extend_12((buf[5] << 8) | buf[4]);
    return ESP_OK;
}

esp_err_t bma400_read_g(bma400_dev_t *dev, bma400_g_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    bma400_raw_t raw;
    esp_err_t err = bma400_read_raw(dev, &raw);
    if (err != ESP_OK) return err;

    out->x_g = raw.x * dev->scale;
    out->y_g = raw.y * dev->scale;
    out->z_g = raw.z * dev->scale;
    return ESP_OK;
}

esp_err_t bma400_read_temperature(bma400_dev_t *dev, float *temp_c)
{
    if (!dev || !temp_c || !dev->i2c_dev) return ESP_ERR_INVALID_ARG;
    uint8_t raw = 0;
    esp_err_t err = rd_reg(dev, BMA400_REG_TEMP, &raw);
    if (err != ESP_OK) return err;
    // Datasheet: temp (°C) = raw * 0.5 + 23  (approx; centered around 23 °C)
    *temp_c = ((int8_t)raw) * 0.5f + 23.0f;
    return ESP_OK;
}

const char *bma400_state_str(bma400_state_t s)
{
    switch (s) {
        case BMA400_STATE_UNINIT:    return "UNINIT";
        case BMA400_STATE_SLEEP:     return "SLEEP";
        case BMA400_STATE_LOW_POWER: return "LOW_POWER";
        case BMA400_STATE_NORMAL:    return "NORMAL";
        case BMA400_STATE_ERROR:     return "ERROR";
        default:                     return "?";
    }
}
