#pragma once
#include <stdint.h>
#include "esp_attr.h"
#include <stdbool.h>
#include "ulp_riscv.h"

#ifdef __cplusplus
extern "C" {
#endif

// These will become ulp_hour, ulp_minute, etc. in the generated header
extern RTC_DATA_ATTR uint8_t hour;
extern RTC_DATA_ATTR uint8_t minute;
extern RTC_DATA_ATTR bool wake_main_now;

#ifdef __cplusplus
}
#endif