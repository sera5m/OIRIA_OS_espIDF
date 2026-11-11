#pragma once
// MAX30102 driver. Replaces the old stub.
// FIFO → short IR window → peak-interval BPM. SpO2 is a rough ratio-of-ratios.
// Does not log; call hr_metrics_push() with the results if you want history.

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#define MAX30102_I2C_ADDR   0x57
#define MAX30102_PART_ID    0x15

// Registers (datasheet)
#define MAX30102_REG_INT_STATUS1  0x00
#define MAX30102_REG_INT_ENABLE1  0x02
#define MAX30102_REG_FIFO_WR      0x04
#define MAX30102_REG_OVF_CTR      0x05
#define MAX30102_REG_FIFO_RD      0x06
#define MAX30102_REG_FIFO_DATA    0x07
#define MAX30102_REG_FIFO_CONFIG  0x08
#define MAX30102_REG_MODE_CONFIG  0x09
#define MAX30102_REG_SPO2_CONFIG  0x0A
#define MAX30102_REG_LED1_PA      0x0C
#define MAX30102_REG_LED2_PA      0x0D
#define MAX30102_REG_TEMP_INT     0x1F
#define MAX30102_REG_TEMP_FRAC    0x20
#define MAX30102_REG_TEMP_CONFIG  0x21
#define MAX30102_REG_REV_ID       0xFE
#define MAX30102_REG_PART_ID      0xFF

#define MAX30102_MODE_HR          0x02
#define MAX30102_MODE_SPO2        0x03

// Short analysis window: 4 s at 50 Hz effective (sample-avg 2 @ 100 Hz).
#define MAX30102_WIN              200

typedef enum {
    MAX30102_STATE_UNINIT = 0,
    MAX30102_STATE_SLEEP,
    MAX30102_STATE_HR,
    MAX30102_STATE_SPO2,
} max30102_state_t;

typedef struct {
    i2c_master_dev_handle_t i2c;
    gpio_num_t int_pin;           // GPIO_NUM_NC if unused
    max30102_state_t state;
    uint8_t sample_hz;            // effective rate after averaging

    // IR/RED window for the current short period.
    uint32_t ir[MAX30102_WIN];
    uint32_t red[MAX30102_WIN];
    uint16_t win_n;

    uint8_t bpm;
    uint8_t spo2;
    bool    beat;                 // true if a peak was seen this update

    // Maxim PBA (peripheral beat amplitude) state. Streaming, one sample
    // at a time. The "window" is the 32-tap FIR delay line plus the DC
    // estimator; there is no separate analysis buffer for BPM.
    struct {
        int16_t ac_cur;
        int16_t ac_prev;
        int16_t ac_min;
        int16_t ac_max;
        int16_t ac_max_hold;
        int16_t ac_min_hold;
        int16_t pos_edge;
        int16_t neg_edge;
        int32_t dc_reg;
        int16_t fir[32];
        uint8_t fir_off;
        int64_t last_beat_us;
    } pba;
} max30102_dev_t;

esp_err_t max30102_init(max30102_dev_t *dev,
                        i2c_master_bus_handle_t bus,
                        gpio_num_t int_pin);

esp_err_t max30102_set_mode(max30102_dev_t *dev, max30102_state_t mode);
esp_err_t max30102_shutdown(max30102_dev_t *dev);

// Drain FIFO into the window and recompute bpm/spo2.
// Pushes a sample to hr_metrics when a new bpm is produced.
esp_err_t max30102_update(max30102_dev_t *dev);

esp_err_t max30102_read_temperature(max30102_dev_t *dev, float *temp_c);

const char *max30102_state_str(max30102_state_t s);

#endif
