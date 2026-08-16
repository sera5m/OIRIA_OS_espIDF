#ifndef MAX30102_SENSOR_H
#define MAX30102_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#define MAX30102_I2C_ADDR 0x57

// Registers
#define REG_INTR_STATUS_1 0x00
#define REG_INTR_ENABLE_1 0x02
#define REG_FIFO_WR_PTR   0x04
#define REG_FIFO_RD_PTR   0x06
#define REG_FIFO_DATA     0x07
#define REG_MODE_CONFIG   0x09
#define REG_SPO2_CONFIG   0x0A
#define REG_LED1_PA       0x0C // Red LED
#define REG_LED2_PA       0x0D // IR LED
#define REG_TEMP_INTEGER  0x1F
#define REG_TEMP_FRACTION 0x20
#define REG_TEMP_CONFIG   0x21

// --- 1. Memory Structures ---

// Stores samples every 2.5s for 1 minute (24 entries)
typedef struct {
    uint8_t seconds_offset; // Time within the 60s frame (0, 2, 5, ...)
    uint8_t heart_rate;     // Calculated BPM
} min_hr_sample_t;

typedef struct {
    min_hr_sample_t samples[24];
    uint8_t sample_count;
} minute_history_t;

// Dynamic ring buffer storing raw IR/RED samples over 5 seconds (5s @ 50Hz = 250 samples)
typedef struct {
    uint32_t ir_buf[250];
    uint32_t red_buf[250];
    size_t head;
    size_t count;
} live_fifo_ringbuf_t;

// --- 2. Modular OS Generic Peripheral Interface ---

typedef struct max30102_dev_t max30102_dev_t;

struct max30102_dev_t {
    i2c_master_dev_handle_t i2c_dev;
    gpio_num_t int_pin;
    
    // Live telemetry states
    live_fifo_ringbuf_t ringbuf;
    minute_history_t min_history;
    uint8_t current_estimated_hr;
    uint8_t current_spo2;
    
    // Virtual Interface function pointers
    esp_err_t (*check_peripheral_data)(max30102_dev_t *dev);
    esp_err_t (*send_ping)(max30102_dev_t *dev);
};

// --- 3. Required API Functions ---

esp_err_t max30102_init(max30102_dev_t *dev, i2c_master_bus_handle_t bus_handle, gpio_num_t int_pin);
esp_err_t max30102_read_hr_spo2(max30102_dev_t *dev, uint8_t *hr, uint8_t *spo2);
esp_err_t max30102_read_temperature(max30102_dev_t *dev, float *temperature);
esp_err_t max30102_read_raw_ir(max30102_dev_t *dev, uint32_t *raw_ir);
esp_err_t max30102_fire_led_pulse(max30102_dev_t *dev, uint8_t red_amplitude, uint8_t ir_amplitude);

// Sleep & Power optimization
esp_err_t max30102_configure_light_sleep_collection(max30102_dev_t *dev);

#endif // MAX30102_SENSOR_H