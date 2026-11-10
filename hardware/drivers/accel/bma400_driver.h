#ifndef BMA400_DRIVER_H
#define BMA400_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

// BMA400 datasheet: BST-BMA400-DS000
// CHIPID = 0x90
// I2C: 0x14 (SDO low) or 0x15 (SDO high)

#define BMA400_CHIP_ID              0x90
#define BMA400_I2C_ADDR_SDO_LOW     0x14
#define BMA400_I2C_ADDR_SDO_HIGH    0x15

// Key registers
#define BMA400_REG_CHIPID           0x00
#define BMA400_REG_ERR              0x02
#define BMA400_REG_STATUS           0x03
#define BMA400_REG_ACC_X_LSB        0x04
#define BMA400_REG_TEMP             0x11
#define BMA400_REG_INT_STAT0        0x0E
#define BMA400_REG_ACC_CONFIG0      0x19
#define BMA400_REG_ACC_CONFIG1      0x1A
#define BMA400_REG_ACC_CONFIG2      0x1B
#define BMA400_REG_INT_CONFIG0      0x1F
#define BMA400_REG_INT_CONFIG1      0x20
#define BMA400_REG_INT1_MAP         0x21
#define BMA400_REG_INT2_MAP         0x22
#define BMA400_REG_INT12_IO_CTRL    0x24
#define BMA400_REG_WKUP_CONFIG0     0x2F
#define BMA400_REG_WKUP_CONFIG1     0x30
#define BMA400_REG_WKUP_CONFIG2     0x31  // ref X
#define BMA400_REG_WKUP_CONFIG3     0x32  // ref Y
#define BMA400_REG_WKUP_CONFIG4     0x33  // ref Z
#define BMA400_REG_CMD              0x7E

// Power modes (ACC_CONFIG0 bits 1:0)
#define BMA400_POWER_SLEEP          0x00
#define BMA400_POWER_LOW            0x01
#define BMA400_POWER_NORMAL         0x02

// Ranges (ACC_CONFIG1 bits 7:6)
#define BMA400_RANGE_2G             0x00
#define BMA400_RANGE_4G             0x01
#define BMA400_RANGE_8G             0x02
#define BMA400_RANGE_16G            0x03

// ODR (ACC_CONFIG1 bits 3:0) — normal mode
#define BMA400_ODR_12_5HZ           0x05
#define BMA400_ODR_25HZ             0x06
#define BMA400_ODR_50HZ             0x07
#define BMA400_ODR_100HZ            0x08
#define BMA400_ODR_200HZ            0x09
#define BMA400_ODR_400HZ            0x0A
#define BMA400_ODR_800HZ            0x0B

// Soft reset command
#define BMA400_CMD_SOFT_RESET       0xB6

// Wake-up interrupt bits
#define BMA400_INT1_MAP_WKUP        (1 << 0)
#define BMA400_INT_STAT0_WKUP       (1 << 0)

// Default shake threshold: ±1.8 g
// At ±2 g range, datasheet: LSB = 2^(2+0)/256 = 15.625 mg → 1.8 / 0.015625 ≈ 115
#define BMA400_SHAKE_THRESHOLD_1_8G 115

// Driver state machine
typedef enum {
    BMA400_STATE_UNINIT = 0,
    BMA400_STATE_SLEEP,
    BMA400_STATE_LOW_POWER,
    BMA400_STATE_NORMAL,
    BMA400_STATE_ERROR
} bma400_state_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} bma400_raw_t;

typedef struct {
    float x_g;
    float y_g;
    float z_g;
} bma400_g_t;

typedef struct bma400_dev_t bma400_dev_t;

struct bma400_dev_t {
    i2c_master_dev_handle_t i2c_dev;
    uint8_t i2c_addr;
    gpio_num_t int1_pin;          // GPIO_NUM_NC if unused
    gpio_num_t int2_pin;          // GPIO_NUM_NC if unused

    bma400_state_t state;
    uint8_t range;                // BMA400_RANGE_*
    uint8_t odr;                  // BMA400_ODR_*
    float scale;                  // g per LSB (for current range)

    // Optional callbacks
    esp_err_t (*on_data_ready)(bma400_dev_t *dev);
    esp_err_t (*on_shake_wake)(bma400_dev_t *dev);  // called from ISR context if set
};

// --- API ---

/**
 * Probe both possible I2C addresses, verify CHIPID == 0x90, soft-reset,
 * then leave the sensor in sleep. Call bma400_set_mode() to start sampling.
 */
esp_err_t bma400_init(bma400_dev_t *dev,
                      i2c_master_bus_handle_t bus,
                      gpio_num_t int1,
                      gpio_num_t int2);

/**
 * State-machine transition. Sleep / low-power / normal.
 * Low-power is fixed 25 Hz; normal uses the configured ODR.
 */
esp_err_t bma400_set_mode(bma400_dev_t *dev, bma400_state_t target);

/**
 * Configure range and ODR. Applied on next transition into normal mode
 * (or immediately if already in normal).
 */
esp_err_t bma400_configure(bma400_dev_t *dev, uint8_t range, uint8_t odr);

/**
 * Configure the wake-up interrupt for shake detection at ±threshold_g
 * (default 1.8 g) and map it to INT1. Puts the sensor into low-power mode.
 * Relative (every-time) reference so gravity is ignored; any axis exceeding
 * the threshold for a few samples asserts INT1.
 *
 * After this, call bma400_arm_cpu_wakeup() to let the ESP32 light-sleep
 * on that pin.
 */
esp_err_t bma400_configure_shake_wake(bma400_dev_t *dev, float threshold_g);

/**
 * Arm the ESP32 to wake from light sleep on the BMA400 INT1 pin
 * (active-high by default after configure_shake_wake).
 * Requires int1_pin to be a real GPIO.
 *
 * NOTE: There is currently no rshell on_wakeup(reason) hook in fboot/sss.
 * After esp_light_sleep_start() returns, the caller must inspect the wake
 * cause (esp_sleep_get_wakeup_cause()) and decide what to do.
 */
esp_err_t bma400_arm_cpu_wakeup(bma400_dev_t *dev);

/**
 * Read raw 12-bit two's-complement samples (only valid in low-power or normal).
 */
esp_err_t bma400_read_raw(bma400_dev_t *dev, bma400_raw_t *out);

/**
 * Read and convert to g using the current scale.
 */
esp_err_t bma400_read_g(bma400_dev_t *dev, bma400_g_t *out);

/**
 * Soft reset. Leaves the device in sleep.
 */
esp_err_t bma400_soft_reset(bma400_dev_t *dev);

/**
 * Read chip temperature (approx °C).
 */
esp_err_t bma400_read_temperature(bma400_dev_t *dev, float *temp_c);

/**
 * Read INT_STAT0 and clear by reading (BMA400 clears on read for most sources).
 */
esp_err_t bma400_read_int_status(bma400_dev_t *dev, uint8_t *status);

const char *bma400_state_str(bma400_state_t s);

#endif // BMA400_DRIVER_H
