// shared_state.h - CORRECT VERSION
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_attr.h"

// ==================== Constants ====================
#define MAX_ALARMS 1
#define MAX_TIMERS 1

// ==================== ULP Structs ====================
typedef struct __attribute__((packed)) {
    uint8_t hour;
    uint8_t minute;
    uint8_t days;
    uint8_t enabled : 1;
    uint8_t vibrate : 1;
    uint8_t repeat_daily : 1;
    uint8_t reserved : 5;
} Alarm;

typedef struct __attribute__((packed)) {
    uint16_t remaining_hours;
    uint8_t remaining_minutes;
    uint8_t running : 1;
    uint8_t should_beep : 1;
    uint8_t paused : 1;
    uint8_t reserved : 5;
    uint8_t id;
} Timer;

// ==================== ULP Shared Memory ====================
// These are the actual RTC variables - declared as extern
// The actual definitions are in ulp_main.c (ULP side)
extern RTC_DATA_ATTR uint8_t ulp_hour;
extern RTC_DATA_ATTR uint8_t ulp_minute;
extern RTC_DATA_ATTR uint16_t ulp_days_since_epoch;
extern RTC_DATA_ATTR uint8_t ulp_alarm_count;
extern RTC_DATA_ATTR uint8_t ulp_timer_count;
extern RTC_DATA_ATTR bool ulp_wake_main_now;
extern RTC_DATA_ATTR bool ulp_has_pending_alert;
extern RTC_DATA_ATTR Alarm ulp_alarms[MAX_ALARMS];
extern RTC_DATA_ATTR Timer ulp_timers[MAX_TIMERS];
extern RTC_DATA_ATTR uint8_t AutoWakeInterval_min;

// ==================== Helper Inline Functions ====================
static inline bool ulp_wake_main_now(void) {
    return ulp_wake_main_now;
}

static inline bool ulp_has_pending_alert(void) {
    return ulp_has_pending_alert;
}

static inline void ulp_clear_wake_flag(void) {
    ulp_wake_main_now = false;
}

static inline void ulp_clear_alert_flag(void) {
    ulp_has_pending_alert = false;
}

// ==================== Function Prototypes ====================
void ulp_init_shared(void);
void ulp_sync_from_main(void);
void ulp_sync_to_main(void);
void ulp_add_alarm(const Alarm* alarm);
bool ulp_remove_alarm(uint8_t index);
void ulp_add_timer(const Timer* timer);
bool ulp_remove_timer(uint8_t index);
void ulp_set_wakeup_interval(uint8_t minutes);
void main_handle_ulp_wakeup(void);

#ifdef __cplusplus
}
#endif