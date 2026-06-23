#pragma once
#include <stdint.h>
#include "esp_attr.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
/*
extern RTC_DATA_ATTR uint8_t ulp_hour;
extern RTC_DATA_ATTR uint8_t ulp_minute;
extern RTC_DATA_ATTR bool ulp_wake_main_now; */
// note to self, DO NOT put RTC_DATA_ATTR here!
extern uint8_t ulp_hour;    // 
extern uint8_t ulp_minute;  // 
extern bool ulp_wake_main_now;  // 
#ifdef __cplusplus
}
#endif