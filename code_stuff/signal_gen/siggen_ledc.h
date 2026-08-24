#pragma once
// LEDC PWM backend — square waves at precise freq; duty-mod DDS for analog-ish
#include "siggen_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t siggen_ledc_init(siggen_state_t* st);
esp_err_t siggen_ledc_start(siggen_state_t* st);
esp_err_t siggen_ledc_stop(siggen_state_t* st);
esp_err_t siggen_ledc_set_freq(siggen_state_t* st, uint32_t freq_hz);
esp_err_t siggen_ledc_set_duty(siggen_state_t* st, uint8_t duty_percent);

// Soft DDS: call from a timer/task to update duty from wavetable sample
// (for sine/tri/saw on a single GPIO + external RC LPF).
esp_err_t siggen_ledc_apply_sample(siggen_state_t* st, int16_t sample_q15);

#ifdef __cplusplus
}
#endif
