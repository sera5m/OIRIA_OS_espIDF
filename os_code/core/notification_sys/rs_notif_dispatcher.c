#include "rs_notif_dispatcher.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "NOTIF";

RTC_DATA_ATTR SharedState shared_state;
SharedState* shared = &shared_state;

void notification_system_init(void) {
    memset(shared, 0, sizeof(SharedState));
    ESP_LOGI(TAG, "Notification system ready (RTC)");
}

bool notification_post(const char* title, const char* msg, uint16_t duration_s,
                       bool immediate, bool run_even_sleep, uint8_t loudness, bool buzzer) {
    if (shared->notif_count >= MAX_NOTIFICATIONS) return false;

    Notification* n = &shared->notifs[shared->notif_count++];
    n->id = shared->next_id++;
    n->duration_s = duration_s ? duration_s : 30;
    n->loudness = loudness;
    n->use_buzzer = buzzer;
    n->run_even_sleep = run_even_sleep;
    n->immediate = immediate;
    n->minutes_delay = immediate ? 0 : 5;
    n->timestamp = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    strncpy(n->title, title ? title : "Alert", sizeof(n->title)-1);
    strncpy(n->message, msg ? msg : "", sizeof(n->message)-1);

    ESP_LOGI(TAG, "Posted: %s", n->title);
    return true;
}

void notification_process(void) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    for (int i = 0; i < shared->notif_count; ) {
        Notification* n = &shared->notifs[i];
        if (n->immediate || ((now - n->timestamp) / 60 >= n->minutes_delay)) {
            //h_alert_dispatch(n->duration_s, n->run_even_sleep, n->loudness, n->use_buzzer);
            shared->notifs[i] = shared->notifs[--shared->notif_count];
        } else i++;
    }
}

void ulp_sync_to_main(void) { /* stub */ }
void main_sync_to_ulp(void) { /* stub */ }

// Simple stubs
void alarm_post(uint8_t h, uint8_t m, uint8_t days, bool en) { /* TODO */ }
void timer_post(uint32_t s, bool r) { /* TODO */ }