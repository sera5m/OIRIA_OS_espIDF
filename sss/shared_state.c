#include "shared_state.h"
#include "esp_log.h"
#include "ulp_main.h"  // Generated header - declares ulp_* variables

// The actual symbol names in the generated header are:
// ulp_ulp_hour, ulp_ulp_minute, ulp_ulp_wake_main_now
// But we'll use the generated header's declarations

static const char *TAG = "SHARED_STATE";
static bool initialized = false;

// Global struct
SharedState shared_state = {0};

esp_err_t shared_state_init(void) {
    if (initialized) return ESP_OK;
    
    // Read initial values
    shared_state_read();
    
    initialized = true;
    ESP_LOGI(TAG, "Shared state initialised");
    shared_state_dump();
    return ESP_OK;
}

esp_err_t shared_state_read(void) {
    if (!initialized) return ESP_ERR_INVALID_STATE;
    
    // Access the ULP variables directly
    // The generated ulp_main.h declares these as extern
    shared_state.hour = ulp_hour;          // These are defined in the generated header
    shared_state.minute = ulp_minute;
    shared_state.wake_main_now = ulp_wake_main_now;
    
    ESP_LOGD(TAG, "Read: %02d:%02d wake=%d",
             shared_state.hour, shared_state.minute, shared_state.wake_main_now);
    return ESP_OK;
}

esp_err_t shared_state_write(void) {
    if (!initialized) return ESP_ERR_INVALID_STATE;
    
    // Write back to ULP variables
    ulp_hour = shared_state.hour;
    ulp_minute = shared_state.minute;
    ulp_wake_main_now = shared_state.wake_main_now;
    
    ESP_LOGD(TAG, "Wrote: %02d:%02d wake=%d",
             shared_state.hour, shared_state.minute, shared_state.wake_main_now);
    return ESP_OK;
}

bool shared_state_should_wake(void) {
    shared_state_read();
    return shared_state.wake_main_now;
}

void shared_state_clear_wake(void) {
    shared_state.wake_main_now = false;
    shared_state_write();
}

void shared_state_dump(void) {
    ESP_LOGI(TAG, "=== Shared State ===");
    ESP_LOGI(TAG, "  Time : %02d:%02d", shared_state.hour, shared_state.minute);
    ESP_LOGI(TAG, "  Wake : %s", shared_state.wake_main_now ? "TRUE" : "FALSE");
    ESP_LOGI(TAG, "====================");
}

void shared_state_set_alarm(uint8_t hour, uint8_t minute, uint8_t days, 
                            bool enabled, bool vibrate, bool repeat_daily) {
    shared_state_read();
    shared_state.alarm_hour = hour;
    shared_state.alarm_minute = minute;
    shared_state.alarm_days = days;
    shared_state.alarm_enabled = enabled;
    shared_state.alarm_vibrate = vibrate;
    shared_state.alarm_repeat_daily = repeat_daily;
    shared_state.alarm_active = true;
    shared_state_write();
    ESP_LOGI(TAG, "Alarm set: %02d:%02d, days=0x%02X, enabled=%d, vibrate=%d, repeat=%d",
             hour, minute, days, enabled, vibrate, repeat_daily);
}

bool shared_state_check_alarm(void) {
    shared_state_read();
    if (shared_state.alarm_active && 
        shared_state.hour == shared_state.alarm_hour &&
        shared_state.minute == shared_state.alarm_minute) {
        shared_state.alarm_active = false;
        shared_state_write();
        return true;
    }
    return false;
}