#pragma once
// Fill sample buffers from precomputed_math (no libm in the hot path)
#include <stdint.h>
#include <stddef.h>
#include "siggen_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fill `n` int16 samples into `dst` starting at *phase, advancing by phase_inc.
// amplitude_q15: 0..32767 scale.
void siggen_fill_sine(int16_t* dst, size_t n, uint32_t* phase, uint32_t phase_inc, int16_t amp_q15);
void siggen_fill_triangle(int16_t* dst, size_t n, uint32_t* phase, uint32_t phase_inc, int16_t amp_q15);
void siggen_fill_saw(int16_t* dst, size_t n, uint32_t* phase, uint32_t phase_inc, int16_t amp_q15);
void siggen_fill_square(int16_t* dst, size_t n, uint32_t* phase, uint32_t phase_inc, int16_t amp_q15, uint8_t duty_pct);
void siggen_fill_noise(int16_t* dst, size_t n, int16_t amp_q15);

// Generic dispatcher
void siggen_fill(siggen_wave_t wave, int16_t* dst, size_t n,
                 uint32_t* phase, uint32_t phase_inc,
                 int16_t amp_q15, uint8_t duty_pct);

#ifdef __cplusplus
}
#endif
