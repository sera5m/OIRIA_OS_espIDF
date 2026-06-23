#include "ulp_main.h"
#include "ulp_riscv.h"
#include "esp_attr.h"
#include <stdbool.h>
RTC_DATA_ATTR uint8_t ulp_hour = 0;
RTC_DATA_ATTR uint8_t ulp_minute = 0;
RTC_DATA_ATTR bool ulp_wake_main_now = false;

void ulp_entry(void) {
    ulp_minute++;
    if (ulp_minute >= 60) {
        ulp_minute = 0;
        ulp_hour = (ulp_hour + 1) % 24;
    }
    if (ulp_minute % 5 == 0) {   // every 5 "minutes" for testing
        ulp_wake_main_now = true;
    }
}

int main(void) {
    ulp_entry();
    return 0;
}