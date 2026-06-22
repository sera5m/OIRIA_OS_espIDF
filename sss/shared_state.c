// shared_state.c
#include "shared_state.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "ULP_SHARED";

// IMPORTANT: These are already defined in ulp_main.c in RTC_SLOW_MEM
// We DO NOT redefine them here - we just declare them as extern in the header
// The linker will resolve them to the ULP's RTC memory locations

void ulp_init_shared(void) {
    // Only initialize what's needed - don't touch variables owned by ULP
    // The ULP will initialize its own variables when it starts
    ESP_LOGI(TAG, "ULP shared memory interface initialized");
    ESP_LOGI(TAG, "ULP memory layout: alarms=%d, timers=%d", MAX_ALARMS, MAX_TIMERS);
}

void ulp_sync_from_main(void) {
    // TODO: Implement real sync from your WatchApp data structures later
    // This would copy main CPU alarm/timer data into ULP memory
}

void ulp_sync_to_main(void) {
    // TODO: Copy ulp_hour / ulp_minute into your display/env later
}

void ulp_add_alarm(const Alarm* alarm) {
    if (ulp_alarm_count >= MAX_ALARMS) {
        ESP_LOGW(TAG, "Cannot add alarm: max reached (%d)", MAX_ALARMS);
        return;
    }
    
    ulp_alarms[ulp_alarm_count].hour         = alarm->hour;
    ulp_alarms[ulp_alarm_count].minute       = alarm->minute;
    ulp_alarms[ulp_alarm_count].days         = alarm->days;
    ulp_alarms[ulp_alarm_count].enabled      = alarm->enabled;
    ulp_alarms[ulp_alarm_count].vibrate      = alarm->vibrate;
    ulp_alarms[ulp_alarm_count].repeat_daily = alarm->repeat_daily;
    
    ulp_alarm_count++;
    ESP_LOGI(TAG, "Added alarm at %02d:%02d (count=%d)", alarm->hour, alarm->minute, ulp_alarm_count);
}

bool ulp_remove_alarm(uint8_t index) {
    if (index >= ulp_alarm_count) return false;
    for (uint8_t i = index; i < ulp_alarm_count - 1; i++) {
        ulp_alarms[i] = ulp_alarms[i + 1];
    }
    ulp_alarm_count--;
    ESP_LOGI(TAG, "Removed alarm at index %d (count=%d)", index, ulp_alarm_count);
    return true;
}

void ulp_add_timer(const Timer* timer) {
    if (ulp_timer_count >= MAX_TIMERS) {
        ESP_LOGW(TAG, "Cannot add timer: max reached (%d)", MAX_TIMERS);
        return;
    }
    ulp_timers[ulp_timer_count] = *timer;
    ulp_timer_count++;
    ESP_LOGI(TAG, "Added timer (count=%d)", ulp_timer_count);
}

bool ulp_remove_timer(uint8_t index) {
    if (index >= ulp_timer_count) return false;
    for (uint8_t i = index; i < ulp_timer_count - 1; i++) {
        ulp_timers[i] = ulp_timers[i + 1];
    }
    ulp_timer_count--;
    ESP_LOGI(TAG, "Removed timer at index %d (count=%d)", index, ulp_timer_count);
    return true;
}

void ulp_set_wakeup_interval(uint8_t minutes) {
    AutoWakeInterval_min = minutes;
    ESP_LOGI(TAG, "Wake interval set to %d minutes", minutes);
}

void main_handle_ulp_wakeup(void) {
    if (!ulp_wake_main_now) return;

    ulp_wake_main_now = false;
    ulp_sync_to_main();

    ESP_LOGI(TAG, "ULP woke main! Pending alert: %d", ulp_has_pending_alert);
    ulp_has_pending_alert = false;

    ulp_sync_from_main();
}