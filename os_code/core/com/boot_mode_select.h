#pragma once
// Early boot: wake-reason check + hold-menu mode select (BIOS-like) + serial rescue.
// Only the GPIO is touched unless the menu button is held or a serial command arrives.
// Chosen role is written to NVS so boot_role_resolve() picks it up.

#include "os_code/core/com/boot_role.hpp"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Primary wake reason. One value; the bitmask below can still carry extras.
typedef enum {
    WAKE_REASON_UNKNOWN = 0,
    WAKE_REASON_BUTTON_PUSH,       // menu / encoder switch
    WAKE_REASON_SENSOR_PING,       // BMA400 shake / sensor INT
    WAKE_REASON_GENERIC_INTERRUPT, // GPIO / EXT0 / EXT1, source not classified
    WAKE_REASON_POWER_RESTORED,    // brownout recovery / power-on after loss
    WAKE_REASON_TIMER,             // ESP timer or ULP timer
    WAKE_REASON_ALARM,             // ULP alarm
    WAKE_REASON_PERIODIC,          // ULP periodic tick
    WAKE_REASON_ULP,               // ULP woke us, reason not further classified
    WAKE_REASON_FORCE,             // explicit force-wake tag
    WAKE_REASON_COUNT
} wake_reason_t;

// Bitmask reasons (ULP + local). Several can be set at once.
#define WAKE_MSG_NONE       0
#define WAKE_MSG_ULP        (1u << 0)
#define WAKE_MSG_ALARM      (1u << 1)
#define WAKE_MSG_TIMER      (1u << 2)
#define WAKE_MSG_PERIODIC   (1u << 3)
#define WAKE_MSG_GPIO       (1u << 4)
#define WAKE_MSG_SHAKE      (1u << 5)
#define WAKE_MSG_FORCE      (1u << 6)  // something requested a forced wake
#define WAKE_MSG_BUTTON     (1u << 7)
#define WAKE_MSG_POWER      (1u << 8)

typedef struct {
    wake_reason_t primary;     // single best reason (enum above)
    uint32_t      reasons;     // bitmask above
    bool          force_wake;  // true if any reason should keep the CPU up
    uint8_t       ulp_raw;     // raw ULP wake_reason if available
} wake_check_t;

const char *wake_reason_name(wake_reason_t r);

// Call as early as possible, before boot_role_resolve().
wake_check_t boot_check_why_awake(void);
wake_check_t boot_get_last_wake(void);

// Poll the menu button (ENCODER0_SW / "sel") for ~200 ms.
bool boot_menu_button_held(void);

// If menu was held, run the minimal mode-select UI (LCD + encoder) and
// accept the same /bootseq commands over the console UART.
// If menu was not held, still listens briefly for a serial rescue command.
// Returns the role that should be used this boot (SOLO if nothing chosen).
boot_role_t boot_mode_select_if_held(void);

// Process one line of the early serial protocol (no leading whitespace).
// Returns true if the command requested an immediate role change / exit.
// Same text can later be fed to rshell.
bool boot_serial_handle_line(const char *line, boot_role_t *out_role);

#ifdef __cplusplus
}
#endif
