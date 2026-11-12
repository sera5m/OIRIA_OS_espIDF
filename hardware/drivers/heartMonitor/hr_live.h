#pragma once
// Live HR session. Enum state machine, same shape as the sensor drivers.
// Holds a dense ring while RUNNING; the passive hr_metrics log is untouched.
// On stop, the ring can be written to the SD as a separate session file.

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HR_LIVE_IDLE = 0,
    HR_LIVE_ARMING,
    HR_LIVE_RUNNING,
    HR_LIVE_PAUSED,
    HR_LIVE_SAVING,
    HR_LIVE_ERROR,
    HR_LIVE_STATE_COUNT
} hr_live_state_t;

#define HR_LIVE_CAP  240   // ~4 min at 1 sample/s, or denser if pushed faster

typedef struct {
    uint8_t  bpm;
    uint8_t  spo2;
    uint8_t  motion_x10;   // | |a|-1g | * 10, clamped
    uint8_t  flags;        // bit0 = motion_reject
} hr_live_sample_t;

esp_err_t        hr_live_init(void);
hr_live_state_t  hr_live_state(void);
const char      *hr_live_state_name(hr_live_state_t s);

// IDLE/PAUSED → ARMING → RUNNING. ERROR → IDLE.
esp_err_t hr_live_start(void);
esp_err_t hr_live_pause(void);
esp_err_t hr_live_resume(void);
// Pack the session to an RLE buffer in RAM, then write it.
// Only call this when the user confirms (ENTER).
esp_err_t hr_live_stop(void);

// RUNNING/PAUSED → IDLE. Drops the session. No file.
esp_err_t hr_live_discard(void);

// Only accepted while RUNNING. Ignored otherwise.
void hr_live_push(uint8_t bpm, uint8_t spo2, float motion_g, bool rejected);

int  hr_live_count(void);
int  hr_live_copy(hr_live_sample_t *out, int max_n);  // oldest first
void hr_live_stats(uint8_t *min_bpm, uint8_t *max_bpm, uint8_t *avg_bpm);

#ifdef __cplusplus
}
#endif
