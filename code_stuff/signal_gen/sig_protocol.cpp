#include "sig_protocol.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include <string.h>

static const char* TAG = "sig_proto";

// ---- SPI ---------------------------------------------------------------------
esp_err_t sig_spi_sink_init(sig_spi_sink_t* s, int host, int cs_gpio,
                            int mosi_gpio, int sclk_gpio, uint32_t clock_hz, uint8_t bits) {
    if (!s) return ESP_ERR_INVALID_ARG;
    memset(s, 0, sizeof(*s));
    s->host = host;
    s->cs_gpio = cs_gpio;
    s->mosi_gpio = mosi_gpio;
    s->sclk_gpio = sclk_gpio;
    s->clock_hz = clock_hz ? clock_hz : 1000000;
    s->bits = (bits == 8) ? 8 : 16;

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = mosi_gpio >= 0 ? mosi_gpio : -1;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = sclk_gpio >= 0 ? sclk_gpio : -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 64;

    esp_err_t e = spi_bus_initialize((spi_host_device_t)host, &buscfg, SPI_DMA_CH_AUTO);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        // INVALID_STATE = already inited — ok for shared bus
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(e));
        return e;
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = (int)s->clock_hz;
    devcfg.mode = 0;
    devcfg.spics_io_num = cs_gpio;
    devcfg.queue_size = 2;
    devcfg.flags = 0;

    spi_device_handle_t h = nullptr;
    e = spi_bus_add_device((spi_host_device_t)host, &devcfg, &h);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(e));
        return e;
    }
    s->handle = h;
    s->inited = true;
    ESP_LOGI(TAG, "SPI sink host=%d cs=%d bits=%u clk=%lu", host, cs_gpio,
             (unsigned)s->bits, (unsigned long)s->clock_hz);
    return ESP_OK;
}

esp_err_t sig_spi_sink_write_sample(sig_spi_sink_t* s, int16_t sample_q15) {
    if (!s || !s->inited || !s->handle) return ESP_ERR_INVALID_STATE;
    // Map Q15 → unsigned mid-scale for common external DACs
    uint16_t u = (uint16_t)((int32_t)sample_q15 + 32768);
    spi_transaction_t t = {};
    if (s->bits <= 8) {
        t.length = 8;
        t.flags = SPI_TRANS_USE_TXDATA;
        t.tx_data[0] = (uint8_t)(u >> 8);
    } else {
        t.length = 16;
        t.flags = SPI_TRANS_USE_TXDATA;
        t.tx_data[0] = (uint8_t)(u >> 8);
        t.tx_data[1] = (uint8_t)(u & 0xFF);
    }
    return spi_device_transmit((spi_device_handle_t)s->handle, &t);
}

esp_err_t sig_spi_sink_write_buf(sig_spi_sink_t* s, const int16_t* samples, size_t n) {
    if (!s || !samples) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < n; ++i) {
        esp_err_t e = sig_spi_sink_write_sample(s, samples[i]);
        if (e != ESP_OK) return e;
    }
    return ESP_OK;
}

void sig_spi_sink_deinit(sig_spi_sink_t* s) {
    if (!s || !s->inited) return;
    if (s->handle) {
        spi_bus_remove_device((spi_device_handle_t)s->handle);
        s->handle = nullptr;
    }
    s->inited = false;
}

// ---- I2C ---------------------------------------------------------------------
esp_err_t sig_i2c_sink_init(sig_i2c_sink_t* s, int port, int sda, int scl,
                            uint8_t addr7, uint8_t reg, uint32_t clock_hz) {
    if (!s) return ESP_ERR_INVALID_ARG;
    memset(s, 0, sizeof(*s));
    s->port = port;
    s->sda_gpio = sda;
    s->scl_gpio = scl;
    s->addr7 = addr7;
    s->reg = reg;
    s->clock_hz = clock_hz ? clock_hz : 400000;

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = port;
    bus_cfg.sda_io_num = (gpio_num_t)sda;
    bus_cfg.scl_io_num = (gpio_num_t)scl;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus = nullptr;
    esp_err_t e = i2c_new_master_bus(&bus_cfg, &bus);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(e));
        return e;
    }
    s->bus = bus;

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = addr7;
    dev_cfg.scl_speed_hz = s->clock_hz;

    i2c_master_dev_handle_t dev = nullptr;
    e = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2c add_device: %s", esp_err_to_name(e));
        i2c_del_master_bus(bus);
        s->bus = nullptr;
        return e;
    }
    s->dev = dev;
    s->inited = true;
    ESP_LOGI(TAG, "I2C sink port=%d addr=0x%02x reg=0x%02x", port, addr7, reg);
    return ESP_OK;
}

esp_err_t sig_i2c_sink_write_sample(sig_i2c_sink_t* s, int16_t sample_q15) {
    if (!s || !s->inited || !s->dev) return ESP_ERR_INVALID_STATE;
    uint16_t u = (uint16_t)((int32_t)sample_q15 + 32768);
    uint8_t buf[3] = { s->reg, (uint8_t)(u >> 8), (uint8_t)(u & 0xFF) };
    return i2c_master_transmit((i2c_master_dev_handle_t)s->dev, buf, 3, 50);
}

void sig_i2c_sink_deinit(sig_i2c_sink_t* s) {
    if (!s || !s->inited) return;
    if (s->dev && s->bus) {
        i2c_master_bus_rm_device((i2c_master_dev_handle_t)s->dev);
        s->dev = nullptr;
    }
    if (s->bus) {
        i2c_del_master_bus((i2c_master_bus_handle_t)s->bus);
        s->bus = nullptr;
    }
    s->inited = false;
}
