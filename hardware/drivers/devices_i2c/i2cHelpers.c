/*
 * i2cHelpers.c
 *
 *  Created on: Mar 19, 2026
 *      Author: dev
 */

#include <stdint.h>
#include "hardware/drivers/devices_i2c/i2cHelpers.h"
/*
static const i2c_device_info_t known_devices[] = {
    // Displays (very common on hobby projects)
    {0x3C, "SSD1306 / SH1106 / SSD1315", "0.96\" / 1.3\" OLED display (most common default)", 98},
    {0x3D, "SSD1306 / SH1106 / SSD1315", "alternative address (via solder jumper or pin)", 92},

    // GPIO / I/O expanders
    {0x20, "MCP23017 / MCP23008 / PCF8574", "16/8-bit I/O expander (common on LCD backpacks)", 90},
    {0x21, "MCP23017 / PCF8574", "I/O expander (A0/A1/A2 pin config)", 85},
    {0x22, "MCP23017 / PCF8574", "I/O expander", 80},
    {0x23, "MCP23017 / PCF8574", "I/O expander", 80},
    {0x24, "MCP23017 / PCF8574", "I/O expander", 80},
    {0x25, "MCP23017 / PCF8574", "I/O expander", 80},
    {0x26, "MCP23017 / PCF8574", "I/O expander", 80},
    {0x27, "PCF8574 / PCF8574A", "I2C LCD backpack (very common 1602/2004 displays)", 95},

    // PWM / Servo drivers
    {0x40, "PCA9685", "16-channel PWM / servo driver (Adafruit, common servo HATs)", 95},
    // PCA9685 can be 0x40-0x7F via address pins — we list the default + note
    {0x41, "PCA9685", "PCA9685 (A0 high)", 70},

    // Temperature / Humidity sensors
    {0x38, "AHT10 / AHT15 / AHT20 / HTU21D / SHT30/31/35", "temp + humidity (common breakout)", 90},
    {0x40, "SI7021 / HTU21D / HDC1080", "temp + humidity", 85},
    {0x44, "SHT31 / SHT4x", "high-precision temp/humidity", 85},

    // Pressure / Altitude sensors
    {0x77, "BMP180 / BMP280 / MS5611 / DPS310", "barometric pressure / altimeter (very common)", 92},
    {0x76, "BMP280 / BME280 / BMP388", "alternative address (SDO pin low/high)", 88},
    {0x29, "BMP388 / BMP390", "high-res pressure", 75},

    // IMUs / Accelerometers / Gyros / Magnetometers
    {0x19, "LSM303 / LIS3DH / LIS2DH", "accel + mag / 3-axis accel", 85},
    {0x1E, "HMC5883 / LSM303 mag", "magnetometer / compass", 80},
    {0x68, "MPU6050 / MPU6500 / MPU9250 / ICM-20948 / DS3231 RTC", "6/9-axis IMU or RTC (very common)", 95},
    {0x6B, "LSM6DS3 / ICM-20689 / MPU6886", "6-axis IMU", 80},
    {0x1C, "MMA845x / FXOS8700", "accel / accel+mag", 75},
    {0x53, "ADXL345 / ADXL343", "3-axis accel (alternative)", 80},

    // RTC (Real Time Clock)
    {0x68, "DS3231 / DS1307", "high-precision RTC (most common module)", 95},

    // ADCs / Current / Power monitors
    {0x48, "ADS1115 / ADS1015", "4-channel 16/12-bit ADC", 90},
    {0x40, "INA219 / INA260", "current/power monitor", 85},
    {0x45, "ADS1115", "alternative address", 70},

    // Other common sensors / modules
    {0x10, "VEML6075 / PA1010D GPS", "UV index / GPS module", 70},
    {0x13, "VCNL4040 / VCNL40x0", "proximity + ambient light", 75},
    {0x29, "VL53L0X / VL53L1X / VL6180X", "time-of-flight distance sensor (very popular)", 90},
    {0x5C, "AM2320", "temp + humidity (alternative to DHT)", 70},
    {0x0B, "LC709203F", "fuel gauge / battery monitor", 65},
    {0x60, "Si5351", "clock generator (common in ham/radio projects)", 75},

    // EEPROM / FRAM / memory
    {0x50, "24LCxx / 24AAxx series EEPROM", "I2C EEPROM (common 256K/512K/etc.)", 90},
    {0x57, "24LCxx with A2 high", "EEPROM alternative", 70},
    // Add more as needed — see sources like i2cdevices.org or Adafruit list
};*/