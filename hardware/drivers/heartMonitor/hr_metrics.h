#pragma once
// Passive heart-rate metrics.
// Short samples accumulate for one minute (up to 128).
// Each minute is reduced 128 → 8 (bucket averages).
// Every 5 minutes the 40 values are run-length encoded into a 1 KB NVS blob.
// At 80% full the blob is appended to the microSD and cleared.

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define HR_METRICS_NVS_SIZE   1024
#define HR_METRICS_FULL_MARK  819   // 80% of 1024

#ifdef __cplusplus
extern "C" {
#endif

// Open the NVS blob (create if missing). Safe to call once at boot.
esp_err_t hr_metrics_init(void);

// Push one short-period sample. bpm == 0 is ignored.
// spo2 may be 0 if unknown.
void hr_metrics_push(uint8_t bpm, uint8_t spo2);

// Close the current minute early (e.g. from a 60 s timer).
// If fewer than 128 samples were pushed, the ones we have are still reduced.
void hr_metrics_end_minute(void);

// Bytes used in the NVS blob, and whether the last flush to SD succeeded.
uint16_t hr_metrics_used(void);
bool hr_metrics_sd_ok(void);

#ifdef __cplusplus
}
#endif
