// os_code/core/intercore_message.c
#include "intercore_message.hpp"
#include "esp_log.h"
#include <string.h>
#include "ulp_riscv.h"

RTC_DATA_ATTR IntercoreMessage intercore_queue[MAX_INTERCORE_MSGS];
RTC_DATA_ATTR uint8_t intercore_queue_count = 0;

static const char* TAG = "INTERCORE";

bool intercore_post_message(const IntercoreMessage* msg) {
    if (intercore_queue_count >= MAX_INTERCORE_MSGS) {
        ESP_LOGW(TAG, "Intercore queue full!");
        return false;
    }
    memcpy(&intercore_queue[intercore_queue_count++], msg, sizeof(IntercoreMessage));
    return true;
}

bool intercore_get_message(IntercoreMessage* out_msg) {
    if (intercore_queue_count == 0) return false;
    memcpy(out_msg, &intercore_queue[0], sizeof(IntercoreMessage));
    memmove(&intercore_queue[0], &intercore_queue[1], 
            (intercore_queue_count-1) * sizeof(IntercoreMessage));
    intercore_queue_count--;
    return true;
}

void intercore_clear_queue() {
    intercore_queue_count = 0;
}