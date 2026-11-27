/*
 * CST816 family touch (CST816S / CST816D / CST816T).
 * I2C, address 0x15. Matches the new ESP-IDF i2c_master API used by BMA400.
 *
 * The datasheet claims real two-point. Common modules and Espressif's own
 * driver only ever return one finger plus a gesture. This driver reads the
 * finger-count register and returns up to CST816_MAX_POINTS. If the chip
 * only gives one, you get one.
 *
 * It only answers I2C after an event. Wire the INT pin, or poll and accept
 * that a read with no touch returns 0 points.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CST816_I2C_ADDR     0x15
#define CST816_MAX_POINTS   2

typedef enum {
    CST816_GESTURE_NONE        = 0x00,
    CST816_GESTURE_SWIPE_UP    = 0x01,
    CST816_GESTURE_SWIPE_DOWN  = 0x02,
    CST816_GESTURE_SWIPE_LEFT  = 0x03,
    CST816_GESTURE_SWIPE_RIGHT = 0x04,
    CST816_GESTURE_SINGLE      = 0x05,
    CST816_GESTURE_DOUBLE      = 0x0B,
    CST816_GESTURE_LONG        = 0x0C,
} cst816_gesture_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t  id;       /* finger id if the chip provides one, else 0 */
} cst816_point_t;

typedef struct {
    uint8_t          n;                        /* 0..CST816_MAX_POINTS */
    cst816_point_t   pt[CST816_MAX_POINTS];
    cst816_gesture_t gesture;
} cst816_report_t;

typedef struct {
    i2c_master_bus_handle_t bus;
    gpio_num_t              rst;   /* GPIO_NUM_NC if unused */
    gpio_num_t              irq;   /* GPIO_NUM_NC if unused */
    uint32_t                speed_hz; /* 0 = 100000 */
} cst816_cfg_t;

typedef struct cst816_dev cst816_dev_t;

/* Adds the device on the bus. Does not require the chip to answer yet. */
esp_err_t cst816_init(const cst816_cfg_t* cfg, cst816_dev_t** out);

/* Optional. Some chips fail init if you read the ID too early. */
esp_err_t cst816_read_id(cst816_dev_t* dev, uint8_t* id);

/* Pulse reset if a pin was given. Safe to call after init. */
esp_err_t cst816_reset(cst816_dev_t* dev);

/* Read one report. n == 0 means no contact. */
esp_err_t cst816_read(cst816_dev_t* dev, cst816_report_t* out);

/* True if the IRQ pin (if given) is asserted. */
bool cst816_irq_asserted(cst816_dev_t* dev);

void cst816_deinit(cst816_dev_t* dev);

#ifdef __cplusplus
}
#endif
