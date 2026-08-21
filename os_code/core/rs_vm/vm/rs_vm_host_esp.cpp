// ESP-IDF host hooks: GPIO + delay + print → ESP_LOG / optional UI sink
#include "rs_vm.hpp"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "rs_vm";

static void host_print_i32(int32_t v, void*) {
    ESP_LOGI(TAG, "%ld", (long)v);
}
static void host_print_str(const char* s, uint8_t len, void*) {
    char tmp[97];
    if (len > 96) len = 96;
    memcpy(tmp, s, len);
    tmp[len] = 0;
    ESP_LOGI(TAG, "%s", tmp);
}
static void host_print_char(char c, void*) {
    ESP_LOGI(TAG, "%c", c);
}
static void host_delay_ms(uint32_t ms, void*) {
    vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}
static void host_pin_mode(uint8_t pin, uint8_t mode, void*) {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << pin;
    io.intr_type = GPIO_INTR_DISABLE;
    if (mode == 1) {
        io.mode = GPIO_MODE_OUTPUT;
        io.pull_up_en = GPIO_PULLUP_DISABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    } else if (mode == 2) {
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    } else {
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_DISABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    }
    gpio_config(&io);
}
static void host_dig_write(uint8_t pin, uint8_t level, void*) {
    gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
}
static int host_dig_read(uint8_t pin, void*) {
    return gpio_get_level((gpio_num_t)pin);
}
static int host_adc_read(uint8_t, void*) {
    return 0; // wire ADC oneshot later
}

extern "C" void rsvm_install_esp_host(rsvm_t* vm) {
    if (!vm) return;
    rsvm_host_t h = {};
    h.print_i32  = host_print_i32;
    h.print_str  = host_print_str;
    h.print_char = host_print_char;
    h.delay_ms   = host_delay_ms;
    h.pin_mode   = host_pin_mode;
    h.dig_write  = host_dig_write;
    h.dig_read   = host_dig_read;
    h.adc_read   = host_adc_read;
    rsvm_set_host(vm, &h);
}
