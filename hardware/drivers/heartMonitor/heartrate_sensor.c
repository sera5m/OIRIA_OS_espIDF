#include "max30102_sensor.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAX30102";

// Helper low-level register write
static esp_err_t write_reg(max30102_dev_t *dev, uint8_t reg, uint8_t data) {
    uint8_t write_buf[2] = {reg, data};
    return i2c_master_transmit(dev->i2c_dev, write_buf, sizeof(write_buf), -1);
}

// Helper low-level register read
static esp_err_t read_regs(max30102_dev_t *dev, uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_transmit_receive(dev->i2c_dev, &reg, 1, data, len, -1);
}

// OS Interface Virtual Functions
static esp_err_t v_send_ping(max30102_dev_t *dev) {
    uint8_t rev_id = 0;
    esp_err_t ret = read_regs(dev, 0xFE, &rev_id, 1); // Read Revision ID
    return (ret == ESP_OK) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t v_check_peripheral_data(max30102_dev_t *dev) {
    uint8_t reg_fifo_wr = 0, reg_fifo_rd = 0;
    read_regs(dev, REG_FIFO_WR_PTR, &reg_fifo_wr, 1);
    read_regs(dev, REG_FIFO_RD_PTR, &reg_fifo_rd, 1);

    int num_samples = (reg_fifo_wr - reg_fifo_rd) & 0x1F;
    if (num_samples == 0) return ESP_OK;

    for (int i = 0; i < num_samples; i++) {
        uint8_t buffer[6];
        read_regs(dev, REG_FIFO_DATA, buffer, 6);

        uint32_t raw_red = ((uint32_t)buffer[0] << 16 | (uint32_t)buffer[1] << 8 | buffer[2]) & 0x03FFFF;
        uint32_t raw_ir  = ((uint32_t)buffer[3] << 16 | (uint32_t)buffer[4] << 8 | buffer[5]) & 0x03FFFF;

        // Push to 5-second ring buffer
        dev->ringbuf.red_buf[dev->ringbuf.head] = raw_red;
        dev->ringbuf.ir_buf[dev->ringbuf.head]  = raw_ir;
        dev->ringbuf.head = (dev->ringbuf.head + 1) % 250;
        if (dev->ringbuf.count < 250) dev->ringbuf.count++;
    }

    return ESP_OK;
}

// --- Driver API Implementation ---

esp_err_t max30102_init(max30102_dev_t *dev, i2c_master_bus_handle_t bus_handle, gpio_num_t int_pin) {
    dev->int_pin = int_pin;
    dev->check_peripheral_data = v_check_peripheral_data;
    dev->send_ping = v_send_ping;

    // Standard ESP-IDF 5.x I2C device setup
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX30102_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev->i2c_dev));

    // Reset sensor
    write_reg(dev, REG_MODE_CONFIG, 0x40);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Configure Default HR/SpO2 Mode (Red + IR LEDs active)
    write_reg(dev, REG_INTR_ENABLE_1, 0xC0); // A_FULL and PP_RDY interrupts
    write_reg(dev, REG_FIFO_WR_PTR, 0x00);
    write_reg(dev, REG_FIFO_RD_PTR, 0x00);
    write_reg(dev, REG_SPO2_CONFIG, 0x27);   // 100 Hz sampling, 411 us pulse width
    write_reg(dev, REG_LED1_PA, 0x24);       // ~7mA for RED
    write_reg(dev, REG_LED2_PA, 0x24);       // ~7mA for IR
    write_reg(dev, REG_MODE_CONFIG, 0x03);   // SpO2 Mode enabled

    return ESP_OK;
}

esp_err_t max30102_read_temperature(max30102_dev_t *dev, float *temperature) {
    write_reg(dev, REG_TEMP_CONFIG, 0x01); // Initiates single temperature reading
    
    // Poll or wait for register readiness
    vTaskDelay(pdMS_TO_TICKS(10));
    
    int8_t temp_int;
    uint8_t temp_frac;
    read_regs(dev, REG_TEMP_INTEGER, (uint8_t*)&temp_int, 1);
    read_regs(dev, REG_TEMP_FRACTION, &temp_frac, 1);

    *temperature = (float)temp_int + ((float)temp_frac * 0.0625f);
    return ESP_OK;
}

esp_err_t max30102_read_raw_ir(max30102_dev_t *dev, uint32_t *raw_ir) {
    if (dev->ringbuf.count == 0) return ESP_ERR_NOT_FINISHED;
    size_t last_idx = (dev->ringbuf.head == 0) ? 249 : dev->ringbuf.head - 1;
    *raw_ir = dev->ringbuf.ir_buf[last_idx];
    return ESP_OK;
}

esp_err_t max30102_fire_led_pulse(max30102_dev_t *dev, uint8_t red_amplitude, uint8_t ir_amplitude) {
    write_reg(dev, REG_LED1_PA, red_amplitude);
    write_reg(dev, REG_LED2_PA, ir_amplitude);
    return ESP_OK;
}

esp_err_t max30102_read_hr_spo2(max30102_dev_t *dev, uint8_t *hr, uint8_t *spo2) {
    *hr = dev->current_estimated_hr;
    *spo2 = dev->current_spo2;
    return ESP_OK;
}

// --- Light Sleep Setup ---
esp_err_t max30102_configure_light_sleep_collection(max30102_dev_t *dev) {
    // 1. Configure MAX30102 internal FIFO interrupt threshold (e.g., wake when 17 samples are unread)
    // Register 0x01 (FIFO Configuration) -> set FIFO_A_FULL bits
    write_reg(dev, 0x01, 0x0F);

    // 2. Configure ESP32 GPIO wake up on INT pin transition (Active Low)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << dev->int_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_LOW_LEVEL
    };
    gpio_config(&io_conf);
    gpio_wakeup_enable(dev->int_pin, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    return ESP_OK;
}