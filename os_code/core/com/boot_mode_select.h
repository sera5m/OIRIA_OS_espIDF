#pragma once
// Early boot: wake-reason check + hold-menu mode select (BIOS-like).
// Only the GPIO is touched unless the menu button is held.
// If held, a minimal LCD + encoder path runs and the chosen role is written
// to NVS so boot_role_resolve() picks it up. Timeout → standard (SOLO).

#include "os_code/core/com/boot_role.hpp"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bitmask reasons (ULP + local).
#define WAKE_MSG_NONE       0
#define WAKE_MSG_ULP        (1u << 0)
#define WAKE_MSG_ALARM      (1u << 1)
#define WAKE_MSG_TIMER      (1u << 2)
#define WAKE_MSG_PERIODIC   (1u << 3)
#define WAKE_MSG_GPIO       (1u << 4)
#define WAKE_MSG_SHAKE      (1u << 5)
#define WAKE_MSG_FORCE      (1u << 6)  // something requested a forced wake

typedef struct {
    uint32_t reasons;          // bitmask above
    bool     force_wake;       // true if any reason should keep the CPU up
    uint8_t  ulp_raw;          // raw ULP wake_reason if available
} wake_check_t;

// Call as early as possible, before boot_role_resolve().
// Reads sleep cause + ULP shared state. Does not start the full OS.
wake_check_t boot_check_why_awake(void);

// Poll the menu button (ENCODER0_SW / "sel") for ~200 ms.
// Returns true if held. Only GPIO is used.
bool boot_menu_button_held(void);

// If menu was held, run the minimal mode-select UI and write the chosen
// role to NVS. Returns the role that should be used this boot.
// If menu was not held, returns BOOT_ROLE_SOLO and does nothing else.
// Special actions (shutdown / flash) are handled inside and do not return
// in the normal sense, or return SOLO after requesting the action.
boot_role_t boot_mode_select_if_held(void);

#ifdef __cplusplus
}
#endif
