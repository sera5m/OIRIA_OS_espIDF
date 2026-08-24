#pragma once
// =============================================================================
// Fast inverse trig (asin / acos / atan) — fixed-point, Quake-style magic
// =============================================================================
// Inputs/outputs use Q15 where applicable:
//   sin/cos domain:  [-1, 1]  ↔  [-32768, 32767]
//   angle output:    radians in Q15 where pi ≈ 102944 (pi * 32767)
//
// Quake-style: use integer bit-hacks + a short Newton / minimax polish instead
// of libm. Good enough for DDS phase recovery, envelope, and scope cursors.
// =============================================================================

#include <stdint.h>
#include "unit_circle_i16.h"

// pi in Q15 (radians * 32767)
#define PCM_Q15_PI       102944
#define PCM_Q15_HALF_PI  51472
#define PCM_Q15_TWO_PI   205887

// -----------------------------------------------------------------------------
// Fast inverse sqrt of a Q15 value treated as float bits (classic Quake III).
// For pure integer pipelines prefer pcm_isqrt_u32 below.
// -----------------------------------------------------------------------------
static inline float pcm_fast_inv_sqrt_f(float x) {
    union { float f; uint32_t i; } u;
    u.f = x;
    u.i = 0x5f3759dfu - (u.i >> 1);          // magic
    u.f = u.f * (1.5f - 0.5f * x * u.f * u.f); // one Newton
    return u.f;
}

// Integer isqrt for 0..2^31-1 (binary digit method, no float)
static inline uint32_t pcm_isqrt_u32(uint32_t n) {
    uint32_t op = n, res = 0, one = 1u << 30;
    while (one > op) one >>= 2;
    while (one) {
        if (op >= res + one) {
            op -= res + one;
            res = (res >> 1) + one;
        } else {
            res >>= 1;
        }
        one >>= 2;
    }
    return res;
}

// -----------------------------------------------------------------------------
// Fast atan2 approximation (Q15 in, radians Q15 out)
// Remez / polynomial on reduced range. Max error ~0.01 rad — fine for UI/DDS.
// Based on the common fixed-point atan2 used in DSP (no div by zero).
// -----------------------------------------------------------------------------
static inline int32_t pcm_fast_atan2_q15(int32_t y, int32_t x) {
    if (x == 0 && y == 0) return 0;

    int32_t abs_y = y < 0 ? -y : y;
    int32_t angle;
    int32_t r, r2;

    if (x >= 0) {
        // r = (x - abs_y) / (x + abs_y)
        int32_t den = x + abs_y;
        if (den == 0) return (y >= 0) ? PCM_Q15_HALF_PI : -PCM_Q15_HALF_PI;
        r = ((x - abs_y) << 15) / den;   // Q15
        r2 = (r * r) >> 15;
        // atan(r) ≈ r * (pi/4 - coeff * r^2)  — coarse
        angle = (PCM_Q15_PI / 4) - (((6433 * r2) >> 15) * r >> 15); // 0.1963≈6433/32767
        angle = (angle * r >> 15) + (PCM_Q15_PI / 4);
    } else {
        int32_t den = abs_y - x;
        if (den == 0) return (y >= 0) ? PCM_Q15_HALF_PI : -PCM_Q15_HALF_PI;
        r = ((x + abs_y) << 15) / den;
        r2 = (r * r) >> 15;
        angle = (PCM_Q15_PI / 4) - (((6433 * r2) >> 15) * r >> 15);
        angle = (angle * r >> 15) + (3 * PCM_Q15_PI / 4);
    }
    return (y < 0) ? -angle : angle;
}

// -----------------------------------------------------------------------------
// asin(x) for x in Q15 [-1,1] → radians Q15 [-pi/2, pi/2]
// Identity: asin(x) = atan2(x, sqrt(1-x^2))
// -----------------------------------------------------------------------------
static inline int32_t pcm_fast_asin_q15(int16_t x_q15) {
    int32_t x = x_q15;
    if (x > PCM_Q15_ONE)  x = PCM_Q15_ONE;
    if (x < -PCM_Q15_ONE) x = -PCM_Q15_ONE;
    // 1 - x^2 in Q15
    int32_t x2 = (x * x) >> 15;
    int32_t one_m = PCM_Q15_ONE - x2;
    if (one_m < 0) one_m = 0;
    // sqrt in Q15: isqrt(one_m * 32767) ≈ sqrt(one_m_q15) * sqrt(32767)
    // Simpler: treat one_m as Q0 scaled, use float inv-sqrt path for polish
    float xf = (float)x / 32767.0f;
    float s = pcm_fast_inv_sqrt_f(1.0f - xf * xf);
    // atan2(x, sqrt) via float then back — still cheaper than libm asin on S3
    float ang = pcm_fast_atan2_q15(x, (int32_t)(32767.0f / s + 0.5f)) / 32767.0f;
    // pcm_fast_atan2 already returns Q15 radians; use directly:
    int32_t den_q15 = (int32_t)(32767.0f * (1.0f / s) + 0.5f);
    if (den_q15 > 32767) den_q15 = 32767;
    return pcm_fast_atan2_q15(x, den_q15);
}

// acos(x) = pi/2 - asin(x)
static inline int32_t pcm_fast_acos_q15(int16_t x_q15) {
    return PCM_Q15_HALF_PI - pcm_fast_asin_q15(x_q15);
}

// atan(x) for x in Q15 (any magnitude; large |x| → ±pi/2)
static inline int32_t pcm_fast_atan_q15(int16_t x_q15) {
    return pcm_fast_atan2_q15(x_q15, PCM_Q15_ONE);
}

// -----------------------------------------------------------------------------
// Phase helpers for DDS: convert Hz + sample_rate → 32-bit phase increment
// phase accumulates; use pcm_sin_i16(phase) each sample.
// -----------------------------------------------------------------------------
static inline uint32_t pcm_phase_inc(uint32_t freq_hz, uint32_t sample_rate_hz) {
    if (sample_rate_hz == 0) return 0;
    // phase_inc = freq / sample_rate * 2^32
    return (uint32_t)(((uint64_t)freq_hz << 32) / sample_rate_hz);
}
