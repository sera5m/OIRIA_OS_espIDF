#ifndef KY040_DRIVER_HPP
#define KY040_DRIVER_HPP

#include <stdint.h>
#include "esp_err.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KY040_DEFAULT_DETENTS_PER_REV  20
#define KY040_PIN_UNUSED               ((gpio_num_t)-1)

typedef void (*ky040_twist_cb_t)(void *user_ctx, int delta);  // +1 CW, -1 CCW

// Opaque handle
typedef struct ky040_impl_t ky040_impl_t;
typedef ky040_impl_t *ky040_handle_t;

typedef struct {
    gpio_num_t          clk_pin;         // A / CLK
    gpio_num_t          dt_pin;          // B / DT
    gpio_num_t          sw_pin;          // KY040_PIN_UNUSED if unused
    uint8_t             detents_per_rev; // usually 20
    ky040_twist_cb_t    on_twist;
    void               *user_ctx;
} ky040_config_t;

esp_err_t ky040_new(const ky040_config_t *config, ky040_handle_t *out_handle);
esp_err_t ky040_del(ky040_handle_t handle);

void      ky040_poll(ky040_handle_t handle);
float     ky040_get_rate_detents_per_sec(ky040_handle_t handle);
float     ky040_get_rate_deg_per_sec(ky040_handle_t handle);  // now non-inline

bool      ky040_is_button_pressed(ky040_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif