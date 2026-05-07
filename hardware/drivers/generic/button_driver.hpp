#ifndef BUTTON_DRIVER_HPP
#define BUTTON_DRIVER_HPP

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct button_impl_t *button_handle_t;

typedef void (*button_cb_t)(void *user_ctx, bool pressed);

typedef struct {
    gpio_num_t pin;          // GPIO number
    bool active_low;         // true if button pulls low when pressed
    uint32_t debounce_ms;    // debounce time in milliseconds (e.g., 50)
    button_cb_t on_press;    // callback when press is confirmed
    button_cb_t on_release;  // callback when release is confirmed (optional)
    void *user_ctx;
} button_config_t;

esp_err_t button_new(const button_config_t *cfg, button_handle_t *out_handle);
esp_err_t button_del(button_handle_t handle);
void button_poll(button_handle_t handle);
bool button_is_pressed(button_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_DRIVER_HPP