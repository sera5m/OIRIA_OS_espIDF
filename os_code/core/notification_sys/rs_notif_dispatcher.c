#include "rs_notif_dispatcher.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "shared_state.h"   // This now properly declares ULP variables
#include <string.h>         // For memset, strncpy
#include "shared_state.h"
// shared_state.c

#include "ulp_riscv.h"      
static const char* TAG = "NOTIF";

#define MAX_NOTIFICATIONS 8
static Notification notifs[MAX_NOTIFICATIONS];
static uint8_t notif_count = 0;

void notification_system_init(void) {
    notif_count = 0;
    memset(notifs, 0, sizeof(notifs));
    shared_state_init();                    // Initialize ULP interface
    ESP_LOGI(TAG, "Notification system ready (using local + ULP)");
}



bool notification_post(const char* title, const char* msg, uint8_t duration_s,
                       bool immediate, bool run_even_sleep, uint8_t loudness, bool buzzer) {
    
    if (notif_count >= MAX_NOTIFICATIONS) return false;

    Notification* n = &notifs[notif_count++];
    n->id = notif_count;  // simple ID
    n->duration_s = duration_s ? duration_s : 30;
    n->loudness = loudness;
    n->use_buzzer = buzzer;
    n->run_even_when_sleep = run_even_sleep;
    n->immediate = immediate;
    n->timestamp = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    strncpy(n->title, title ? title : "Alert", sizeof(n->title)-1);
    strncpy(n->message, msg ? msg : "", sizeof(n->message)-1);

    ESP_LOGI(TAG, "Posted: %s", n->title);

    if (immediate) {
        // TODO: call your alert handler here
    }

    return true;
}

void notification_process(void) {
    // Process immediate notifications, etc.
    // For now just a stub
}




void ulp_add_alarm_from_main(uint8_t hour, uint8_t minute, uint8_t days, bool enabled, bool vibrate, bool repeat_daily) {
    
    // For now, just store it in RTC memory if you have alarm fields
    // Or call shared_state functions
    ESP_LOGI("ALARM", "Alarm set: %02d:%02d (stub - implement later)", hour, minute);
    
    // If you add alarm fields to SharedState struct:
    // shared_state_read();
    // shared_state.alarm_hour = hour;
    // shared_state.alarm_minute = minute;
    // shared_state_write();
}

void ulp_add_timer_from_main(uint32_t seconds, uint8_t id) {
    ESP_LOGI("NOTIF", "Timer %d sec (stub)", seconds);
}