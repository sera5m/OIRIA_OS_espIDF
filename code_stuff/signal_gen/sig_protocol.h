#pragma once
// protocol_spi / protocol_i2c — push signal samples (or packed frames) on the wire
// Used as nested sinks from sig_insn (SIG_OP_OUT_SPI / SIG_OP_OUT_I2C).
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- SPI sink ----------------------------------------------------------------
// spi_host: 1=SPI2, 2=SPI3 (S3). bits: 8 or 16 sample packing.
typedef struct {
    int      host;       // SPI2_HOST / SPI3_HOST as int
    int      cs_gpio;
    int      mosi_gpio;  // -1 = leave matrix as previously configured
    int      sclk_gpio;
    uint32_t clock_hz;
    uint8_t  bits;       // 8 or 16
    bool     inited;
    void*    handle;     // spi_device_handle_t
} sig_spi_sink_t;

esp_err_t sig_spi_sink_init(sig_spi_sink_t* s, int host, int cs_gpio,
                            int mosi_gpio, int sclk_gpio, uint32_t clock_hz, uint8_t bits);
esp_err_t sig_spi_sink_write_sample(sig_spi_sink_t* s, int16_t sample_q15);
esp_err_t sig_spi_sink_write_buf(sig_spi_sink_t* s, const int16_t* samples, size_t n);
void      sig_spi_sink_deinit(sig_spi_sink_t* s);

// ---- I2C sink ----------------------------------------------------------------
// Writes sample as 16-bit big-endian to reg on device addr (common DAC/codec pattern).
typedef struct {
    int      port;       // 0 or 1
    int      sda_gpio;
    int      scl_gpio;
    uint8_t  addr7;      // 7-bit
    uint8_t  reg;
    uint32_t clock_hz;
    bool     inited;
    void*    bus;        // i2c_master_bus_handle_t
    void*    dev;        // i2c_master_dev_handle_t
} sig_i2c_sink_t;

esp_err_t sig_i2c_sink_init(sig_i2c_sink_t* s, int port, int sda, int scl,
                            uint8_t addr7, uint8_t reg, uint32_t clock_hz);
esp_err_t sig_i2c_sink_write_sample(sig_i2c_sink_t* s, int16_t sample_q15);
void      sig_i2c_sink_deinit(sig_i2c_sink_t* s);

#ifdef __cplusplus
}
#endif
