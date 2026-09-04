#pragma once
#include <stdint.h>
#include "rsvm_target.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const int16_t rsvm_sin_q15[256];

/* phase: 8.8 fixed, 256 == one turn */
static inline int16_t rsvm_sin_q15_at(uint8_t idx) {
    return rsvm_sin_q15[idx];
}
static inline int16_t rsvm_cos_q15_at(uint8_t idx) {
    return rsvm_sin_q15[(uint8_t)(idx + 64)]; /* +90 deg in 256-pt table */
}

/* i32 degrees * 256 / 360, cheap wrap */
static inline uint8_t rsvm_phase8_from_deg(int32_t deg) {
    int32_t p = deg % 360;
    if (p < 0) p += 360;
    return (uint8_t)((p * 256) / 360);
}

#ifdef __cplusplus
}
#endif
