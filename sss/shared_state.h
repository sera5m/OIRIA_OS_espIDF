#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
//#include "esp32s3/ulp_riscv.h"  // ← Some IDF versions need the full path
//#include "ulp_riscv/ulp_riscv.h"


#ifdef __cplusplus
extern "C" {
#endif
//void *ulp_riscv_get_symbol(const char *name);

// This struct mirrors the ULP's RTC variables.
// Keep it in sync with ulp_main.c!

typedef struct {
    uint8_t hour;
    uint8_t minute;
    bool wake_main_now;
    
    // Alarm fields (for future use)
    uint8_t alarm_hour;
    uint8_t alarm_minute;
    uint8_t alarm_days;
    bool alarm_enabled;
    bool alarm_vibrate;
    bool alarm_repeat_daily;
    bool alarm_active;
} SharedState;


// Global handle – use this like a normal struct.
extern SharedState shared_state;

// ─── Initialisation ────────────────────────────────────────
esp_err_t shared_state_init(void);   // calls ulp_riscv_get_symbol, reads initial values

// ─── Sync with ULP memory ──────────────────────────────────
esp_err_t shared_state_read(void);   // copy ULP → shared_state
esp_err_t shared_state_write(void);  // copy shared_state → ULP

// ─── Convenience helpers ──────────────────────────────────
bool shared_state_should_wake(void); // read + return wake flag
void shared_state_clear_wake(void);  // clear flag and write back

// Debug
void shared_state_dump(void);



// shared_state.h - add these declarations
void shared_state_set_alarm(uint8_t hour, uint8_t minute, uint8_t days, 
    bool enabled, bool vibrate, bool repeat_daily);  // 6 params


bool shared_state_check_alarm(void);

// ─── Optional macro magic (auto-sync) ──────────────────────
#define SHARED_READ(var) do { shared_state_read(); var = shared_state.var; } while(0)
#define SHARED_WRITE(var, val) do { shared_state.var = val; shared_state_write(); } while(0)
#define SHARED_GET(var) (shared_state_read(), shared_state.var)
#define SHARED_SET(var, val) (shared_state.var = val, shared_state_write())

#ifdef __cplusplus
}
#endif