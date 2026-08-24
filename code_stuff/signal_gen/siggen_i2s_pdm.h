#pragma once
// I2S PDM TX — analog-ish output on ESP32-S3 (no hardware DAC).
// Pair data pin with a simple RC low-pass (~1–10 kHz cutoff) for usable AC.
#include "siggen_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t siggen_i2s_pdm_init(siggen_state_t* st);
esp_err_t siggen_i2s_pdm_start(siggen_state_t* st);
esp_err_t siggen_i2s_pdm_stop(siggen_state_t* st);

// Blocking write of one buffer of int16 PCM (converted to PDM by driver).
esp_err_t siggen_i2s_pdm_write(siggen_state_t* st, const int16_t* samples, size_t n_samples);

// FreeRTOS task helper: continuously fills + writes the configured wave.
void siggen_i2s_pdm_task(void* arg);  // arg = siggen_state_t*

#ifdef __cplusplus
}
#endif
