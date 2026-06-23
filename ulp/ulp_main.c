#include "ulp_main.h"
#include "ulp_riscv.h"
#include "esp_attr.h"
#include <stdbool.h>

RTC_DATA_ATTR uint8_t hour = 0;        // Was ulp_hour
RTC_DATA_ATTR uint8_t minute = 0;      // Was ulp_minute
RTC_DATA_ATTR bool wake_main_now = false; // Was ulp_wake_main_now

void ulp_entry(void) {
    minute++;
    if (minute >= 60) {
        minute = 0;
        hour = (hour + 1) % 24;
    }
    if (minute % 5 == 0) {
        wake_main_now = true;
    }
}

int main(void) {
    ulp_entry();
    return 0;
}