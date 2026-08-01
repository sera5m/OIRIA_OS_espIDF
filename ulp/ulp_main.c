#include "ulp_main.h"
#include "ulp_riscv.h"
#include "esp_attr.h"
#include <stdbool.h>

RTC_DATA_ATTR uint8_t  hour              = 0;
RTC_DATA_ATTR uint8_t  minute            = 0;
RTC_DATA_ATTR bool     wake_main_now     = false;
RTC_DATA_ATTR uint8_t  wake_reason       = 0;

RTC_DATA_ATTR uint16_t timer0_remain_min = 0;
RTC_DATA_ATTR uint16_t timer1_remain_min = 0;
RTC_DATA_ATTR uint16_t timer2_remain_min = 0;
RTC_DATA_ATTR uint8_t  timer_running     = 0;

RTC_DATA_ATTR uint8_t  alarm0_hh = 8;
RTC_DATA_ATTR uint8_t  alarm0_mm = 0;
RTC_DATA_ATTR uint8_t  alarm1_hh = 0;
RTC_DATA_ATTR uint8_t  alarm1_mm = 0;
RTC_DATA_ATTR uint8_t  alarm2_hh = 0;
RTC_DATA_ATTR uint8_t  alarm2_mm = 0;
RTC_DATA_ATTR uint8_t  alarm3_hh = 0;
RTC_DATA_ATTR uint8_t  alarm3_mm = 0;
RTC_DATA_ATTR uint8_t  alarm_enabled = 0;
RTC_DATA_ATTR uint8_t  alarm_fired   = 0;

static uint16_t get_timer_remain(int i)
{
    switch (i) {
        case 0: return timer0_remain_min;
        case 1: return timer1_remain_min;
        case 2: return timer2_remain_min;
        default: return 0;
    }
}

static void set_timer_remain(int i, uint16_t v)
{
    switch (i) {
        case 0: timer0_remain_min = v; break;
        case 1: timer1_remain_min = v; break;
        case 2: timer2_remain_min = v; break;
        default: break;
    }
}

static void get_alarm(int i, uint8_t *hh, uint8_t *mm)
{
    switch (i) {
        case 0: *hh = alarm0_hh; *mm = alarm0_mm; break;
        case 1: *hh = alarm1_hh; *mm = alarm1_mm; break;
        case 2: *hh = alarm2_hh; *mm = alarm2_mm; break;
        case 3: *hh = alarm3_hh; *mm = alarm3_mm; break;
        default: *hh = 0; *mm = 0; break;
    }
}

void ulp_entry(void)
{
    minute++;
    if (minute >= 60) {
        minute = 0;
        hour = (hour + 1) % 24;
    }

    uint8_t reason = 0;

    for (int i = 0; i < ULP_TIMER_COUNT; i++) {
        if (timer_running & (1u << i)) {
            uint16_t rem = get_timer_remain(i);
            if (rem > 0) {
                rem--;
                set_timer_remain(i, rem);
            }
            if (rem == 0) {
                timer_running &= ~(1u << i);
                reason |= ULP_WAKE_TIMER;
            }
        }
    }

    for (int i = 0; i < ULP_ALARM_COUNT; i++) {
        if (alarm_enabled & (1u << i)) {
            uint8_t ah, am;
            get_alarm(i, &ah, &am);
            if (ah == hour && am == minute) {
                alarm_fired |= (1u << i);
                reason |= ULP_WAKE_ALARM;
            }
        }
    }

    if (minute % 5 == 0) {
        reason |= ULP_WAKE_PERIODIC;
    }

    if (reason != 0) {
        wake_reason   = reason;
        wake_main_now = true;
    }
}

int main(void)
{
    ulp_entry();
    return 0;
}
