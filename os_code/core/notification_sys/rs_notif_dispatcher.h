#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MAX_NOTIFICATIONS 16
#define MAX_ALARMS        8
#define MAX_TIMERS        6

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint16_t duration_s;
    uint8_t  loudness;
    bool     use_buzzer : 1;
    bool     run_even_sleep : 1;
    bool     immediate : 1;
    uint8_t  minutes_delay;
    uint32_t timestamp;
    char     title[24];
    char     message[48];
} Notification;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  days_bitmask;   // 0=Sun ... 6=Sat
    bool     enabled : 1;
    bool     snooze : 1;
} Alarm;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t remaining_seconds;
    bool     running : 1;
    bool     repeat : 1;
} Timer;

typedef struct __attribute__((packed)) {
    Notification notifs[MAX_NOTIFICATIONS];
    Alarm        alarms[MAX_ALARMS];
    Timer        timers[MAX_TIMERS];
    
    uint8_t notif_count;
    uint8_t alarm_count;
    uint8_t timer_count;
    uint8_t next_id;
} SharedState;

// API
void notification_system_init(void);
bool notification_post(const char* title, const char* msg, uint16_t duration_s,
                       bool immediate, bool run_even_sleep, uint8_t loudness, bool buzzer);

void alarm_post(uint8_t hour, uint8_t minute, uint8_t days_bitmask, bool enabled);
void timer_post(uint32_t seconds, bool repeat);

void notification_process(void);
void ulp_sync_to_main(void);
void main_sync_to_ulp(void);

extern SharedState* shared;


#ifdef __cplusplus
}
#endif