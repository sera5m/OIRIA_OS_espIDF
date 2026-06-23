#include "shared_state.h"
#include "esp_log.h"
#include "ulp_main.h"
static const char* TAG = "ULP";

void ulp_init_shared(void) {
  //  ESP_LOGI(TAG, "ULP ready - Time: %02d:%02d", ulp_hour, ulp_minute);
  ESP_LOGI(TAG, "ulp?");
}

void ulp_add_alarm(uint8_t hour, uint8_t minute) {
    ESP_LOGI(TAG, "Alarm set: %02d:%02d (stub)", hour, minute);
}

void main_handle_ulp_wakeup(void) {
    if (ulp_wake_main_now) {
        ESP_LOGW(TAG, "=== ULP WAKEUP! Time %02d:%02d ===", ulp_hour, ulp_minute);
        ulp_wake_main_now = false;
    }
}