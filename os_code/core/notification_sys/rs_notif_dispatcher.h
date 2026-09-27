#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_attr.h"

// Keep your Notification type (notifications stay on main CPU for now)
typedef struct {
    uint32_t id;
    uint16_t duration_s;
    uint8_t  loudness;
    bool     use_buzzer;
    bool     run_even_when_sleep;
    bool     immediate;
    uint32_t timestamp;
    char     title[24];
    char     message[32];
} Notification;

void notification_system_init(void);
bool notification_post(const char* title, const char* msg, uint8_t duration_s,
                       bool immediate, bool run_even_sleep, uint8_t loudness, bool buzzer);

void notification_process(void);

void ulp_add_alarm_from_main(uint8_t hour, uint8_t minute, uint8_t days, bool enabled, bool vibrate, bool repeat_daily);
void ulp_add_timer_from_main(uint32_t seconds, uint8_t id);

#ifdef __cplusplus
}
#endif
