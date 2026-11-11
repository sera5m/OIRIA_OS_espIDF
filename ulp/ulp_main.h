#pragma once
#include <stdint.h>
#include "esp_attr.h"
#include <stdbool.h>
#include "ulp_riscv.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared RTC variables between ULP RISC-V and main CPU.
 * After ESP-IDF ULP build these appear as ulp_hour, ulp_minute, etc.
 * Do NOT use C arrays – IDF flattens them into a single uint32_t.
 * ULP is expected to be woken once per minute by the ULP timer.
 */

extern RTC_DATA_ATTR uint8_t hour;
extern RTC_DATA_ATTR uint8_t minute;
extern RTC_DATA_ATTR bool    wake_main_now;
extern RTC_DATA_ATTR uint8_t wake_reason;

#define ULP_WAKE_PERIODIC   (1u << 0)
#define ULP_WAKE_ALARM      (1u << 1)
#define ULP_WAKE_TIMER      (1u << 2)
#define ULP_WAKE_MANUAL     (1u << 3)
#define ULP_WAKE_SHAKE      (1u << 4)   // BMA400 INT1 force-wake tag
#define ULP_WAKE_FORCE      (1u << 5)   // generic force-wake tag

// Main CPU (or BMA path) sets this; ULP ORs it into wake_reason on next tick.
extern RTC_DATA_ATTR uint8_t force_tag;

#define ULP_TIMER_COUNT 3
extern RTC_DATA_ATTR uint16_t timer0_remain_min;
extern RTC_DATA_ATTR uint16_t timer1_remain_min;
extern RTC_DATA_ATTR uint16_t timer2_remain_min;
extern RTC_DATA_ATTR uint8_t  timer_running;

#define ULP_ALARM_COUNT 4
extern RTC_DATA_ATTR uint8_t alarm0_hh;
extern RTC_DATA_ATTR uint8_t alarm0_mm;
extern RTC_DATA_ATTR uint8_t alarm1_hh;
extern RTC_DATA_ATTR uint8_t alarm1_mm;
extern RTC_DATA_ATTR uint8_t alarm2_hh;
extern RTC_DATA_ATTR uint8_t alarm2_mm;
extern RTC_DATA_ATTR uint8_t alarm3_hh;
extern RTC_DATA_ATTR uint8_t alarm3_mm;
extern RTC_DATA_ATTR uint8_t alarm_enabled;
extern RTC_DATA_ATTR uint8_t alarm_fired;

#ifdef __cplusplus
}
#endif
