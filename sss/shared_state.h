#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_attr.h"
#include "ulp/ulp_main.h"   // ← note the path

#ifdef __cplusplus
extern "C" {
#endif

// No need to re-declare — ulp_main.h already does it

void ulp_init_shared(void);
void ulp_add_alarm(uint8_t hour, uint8_t minute);
void main_handle_ulp_wakeup(void);

#ifdef __cplusplus
}
#endif