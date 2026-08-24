#pragma once
// High-level controller — call from apps, menu, or Vulcan OP_NATIVE wrappers
#include "siggen_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Singleton convenience (one generator per node; expand to array later)
siggen_state_t* siggen_get(void);

esp_err_t siggen_configure(const siggen_cfg_t* cfg);
esp_err_t siggen_start(void);
esp_err_t siggen_stop(void);
esp_err_t siggen_set_freq(uint32_t freq_hz);
esp_err_t siggen_set_wave(siggen_wave_t wave);
esp_err_t siggen_set_duty(uint8_t duty_percent);

// Snapshot for UI / Vulcan env
const siggen_cfg_t* siggen_cfg_snapshot(void);

#ifdef __cplusplus
}
#endif
