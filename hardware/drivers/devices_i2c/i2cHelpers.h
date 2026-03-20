/*
 * i2cHelpers.h
 *
 *  Created on: Mar 19, 2026
 *      Author: dev
 */

#ifndef MAIN_HARDWARE_DRIVERS_DEVICES_I2C_I2CHELPERS_H_
#define MAIN_HARDWARE_DRIVERS_DEVICES_I2C_I2CHELPERS_H_


typedef struct {
    uint8_t addr;
    const char *name;
    const char *extra;      // e.g. variants, notes
    int confidence;         // 0-100, arbitrary
} i2c_device_info_t;

static const i2c_device_info_t known_devices[];



#endif /* MAIN_HARDWARE_DRIVERS_DEVICES_I2C_I2CHELPERS_H_ */
