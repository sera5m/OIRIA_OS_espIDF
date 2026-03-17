#ifndef KY040_DRIVER_HPP
#define KY040_DRIVER_HPP

#include <stdint.h>
#include "esp_err.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"


#define CountsPerPhysicalDetent 2
// Many KY-040 encoders produce ±4 or ±2 counts per physical detent/click (due to quadrature states).
//If you divide by 2 but the encoder actually gives ±4, you'll miss every other click.
//here we have 1, but feel free to change later
//i should probably use a virtual function for this

#define STEP_THRESHOLD      2       // how many raw steps needed to count as 1 detent
//was 3 at one point but i'm fairly sure it's physically 2, keep it at 2
#define ABANDON_TIMEOUT_US  550000  // 550 ms — if no progress, forget half-turn

//when rotating the dials the turn often happens in one raw step, pause, then the next, because humans are slow creatures
//for future information, 
/*
Without long timeout (or with short/no timeout):
Slow turns produce 1–2 raw steps → then long pause → timeout resets pending_steps to 0 before next step arrives → never reaches threshold → no callback.
With long timeout (450–800 ms):
Step 1 arrives (pending = 1) → pause 300 ms (still within timeout) → step 2 arrives → pending = 2 → step 3 arrives → threshold hit → callback fires.
The software "remembers" the intent across those long pauses.
*/

#ifdef __cplusplus
extern "C" {
#endif

#define KY040_DEFAULT_DETENTS_PER_REV  20 
//THIS DIFFERS PER UNIT. IT'S USUALLY 20
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

//notes on the sen sor itself:
/*
Mechanical bounce / incomplete state transitions during slow rotation
KY-040 encoders are cheap mechanical parts with significant contact bounce and imperfect quadrature alignment at low speeds. Slow turns let the contacts "hover" or chatter in invalid states (e.g., both CLK and DT high/low too long or bounce without full transition).
PCNT quadrature mode expects clean, valid state changes. If a transition is "stuck" or bounces without completing the full 00 → 01 → 11 → 10 cycle, PCNT can miss it or count erratically.
PCNT quadrature decoding mode subtlety
The current config uses PCNT_CHANNEL_EDGE_ACTION_INCREASE on rising CLK + DT level for direction. This is standard, but on real hardware with bounce, it can miss slow/partial transitions.
Polling interval vs. how PCNT accumulates
If polling is too slow relative to bounce settling time, you might read the counter after bounce has "canceled" some counts (e.g. +1 then -1 from bounce).
*/