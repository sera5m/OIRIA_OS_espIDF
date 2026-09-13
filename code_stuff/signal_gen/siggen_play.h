#pragma once
// Unified play / stop / scope / corz-cmd front for siggen_ctrl.
// Same firmware on head (tyrant/solo) and secondary (puppet).
#include "siggen_types.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SIGGEN_DEFAULT_OUT_GPIO
#define SIGGEN_DEFAULT_OUT_GPIO  4
#endif
#ifndef SIGGEN_DEFAULT_SCOPE_GPIO
#define SIGGEN_DEFAULT_SCOPE_GPIO 1
#endif

// wave: 0 sine, 1 square, 2 triangle, 3 saw, 4 noise
esp_err_t siggen_play(int wave, uint32_t freq_hz, uint8_t duty_pct, int gpio, uint8_t amp_pct);
esp_err_t siggen_play_stop(void);
esp_err_t siggen_play_set_freq(uint32_t freq_hz);
esp_err_t siggen_play_set_duty(uint8_t duty_pct);
esp_err_t siggen_play_set_amp(uint8_t amp_pct);
esp_err_t siggen_play_sweep(uint32_t f0, uint32_t f1, uint32_t ms);

const siggen_cfg_t* siggen_play_cfg(void);
int                 siggen_play_gpio(void);

// ADC capture (Bojan-style oneshot). Returns count filled; mv may be NULL.
int siggen_scope_cap(int gpio, int n, int16_t* mv, int16_t* raw);
int siggen_adc_once(int gpio);   // millivolts, or -1

// Corz-style one-liners: "s" "r" "t" "2000" "2k" "p25" "a2" "stop" "sweep 100 2k 2000"
// Returns 0 ok, -1 unknown. Writes a short status into out.
int siggen_corz_cmd(const char* cmd, char* out, int out_max);

#ifdef __cplusplus
}
#endif
