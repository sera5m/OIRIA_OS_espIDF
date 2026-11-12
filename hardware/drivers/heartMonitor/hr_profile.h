#pragma once
// Local statistical profile. No cloud, no baseline from a study.
// Tracks the user's own BPM and SpO2 mean/spread, and a crude RMSSD
// from beat-to-beat intervals. Bands are mean ± 2 sd.

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hr_profile_init(void);

// One accepted sample. interval_ms is the time since the previous beat
// (0 if unknown). Used for RMSSD.
void hr_profile_push(uint8_t bpm, uint8_t spo2, uint32_t interval_ms);

typedef struct {
    uint16_t n;
    uint8_t  bpm_mean;
    uint8_t  bpm_sd;
    uint8_t  bpm_lo;     // mean - 2 sd, clamped
    uint8_t  bpm_hi;
    uint8_t  spo2_mean;
    uint8_t  spo2_sd;
    uint16_t hrv_rmssd_ms;
    bool     hrv_valid;
} hr_profile_t;

void hr_profile_get(hr_profile_t *out);

// 0 = inside band, -1 = below, +1 = above. Needs a few samples first.
int hr_profile_bpm_band(uint8_t bpm);

#ifdef __cplusplus
}
#endif
