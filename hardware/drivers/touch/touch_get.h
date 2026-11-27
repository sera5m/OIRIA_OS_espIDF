/*
 * Get side only. Reads the CST816, applies TouchConf filters,
 * and hands back points or InputEvents. Processing is the caller's.
 */
#pragma once

#include "cst816.h"
#include "touchscreen_cfg.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One filtered point. screen_x/y are after scale. dx/dy are since last call. */
typedef struct {
    uint8_t  id;
    uint16_t screen_x;
    uint16_t screen_y;
    int16_t  dx;
    int16_t  dy;
    uint8_t  down;          /* 1 if this finger is in contact */
} touch_point_out_t;

typedef struct {
    uint8_t           n;
    touch_point_out_t pt[CST816_MAX_POINTS];
    cst816_gesture_t  gesture;
} touch_filtered_t;

/* Attach the chip on an existing bus. rst/irq may be GPIO_NUM_NC. */
esp_err_t touch_open(i2c_master_bus_handle_t bus,
                     gpio_num_t rst, gpio_num_t irq,
                     cst816_dev_t** out);

/* Read once. Applies scale, deadzone, and max_touch_points from cfg.
 * Last-point state lives in *prev (pass NULL the first time, or zero it). */
esp_err_t touch_get_report(cst816_dev_t* dev, const TouchConf* cfg,
                           touch_filtered_t* prev,
                           touch_filtered_t* out);

/* Same read, packed as one InputEvent per finger.
 * key = finger id. action = Press/Release/PositionDelta.
 * Returns the number written, capped at cap. */
int touch_get_events(cst816_dev_t* dev, const TouchConf* cfg,
                     touch_filtered_t* prev,
                     InputEvent* ev, int cap);

#ifdef __cplusplus
}
#endif
