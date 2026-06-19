// os_code/core/intercore_message.hpp
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MAX_INTERCORE_MSGS 8
#define MAX_NOTIF_TITLE 24
#define MAX_NOTIF_MSG   48

typedef enum {
    MSG_NONE = 0,
    MSG_SET_ALARM,
    MSG_SET_TIMER,
    MSG_CANCEL_ALARM,
    MSG_DISPATCH_ALERT,     // ULP → Main: "wake up and alert"
    MSG_SYNC_TIME,
    MSG_POWER_STATE_CHANGE,
    MSG_DUMP_NOTIFS
} intercore_msg_type_t;

typedef struct {
    intercore_msg_type_t type;
    uint32_t timestamp;

    union {
        struct {                    // SET_ALARM / SET_TIMER
            uint8_t  hour;
            uint8_t  minute;
            uint16_t duration_minutes;
            uint8_t  days_bitmask;  // 0b00111111 = Mon-Sun
            bool     repeat;
            bool     vibrate;
        } alarm;

        struct {                    // DISPATCH_ALERT
            uint16_t duration_s;
            uint8_t  loudness;
            bool     use_buzzer;
            bool     run_even_sleep;
            char     title[MAX_NOTIF_TITLE];
            char     message[MAX_NOTIF_MSG];
        } alert;
    };
} IntercoreMessage;

// Shared buffer in RTC memory (survives deep sleep)
extern IntercoreMessage intercore_queue[MAX_INTERCORE_MSGS];
extern uint8_t intercore_queue_count;

// API (works from both main core and ULP)
bool intercore_post_message(const IntercoreMessage* msg);
bool intercore_get_message(IntercoreMessage* out_msg); // main core consumes
void intercore_clear_queue();