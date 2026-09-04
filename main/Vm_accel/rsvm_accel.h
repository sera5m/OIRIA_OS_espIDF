#pragma once
#include <stdint.h>
#include "rsvm_target.h"
#include "rsvm_trig_lut.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fill dst[n] with sin (Q15) walking phase += step. Sequential table hits. */
void rsvm_accel_osc_q15(int16_t* dst, uint16_t n, uint8_t phase0, uint8_t step);

#if RSVM_ACCEL_HAS_JIT
/* Desktop-only hook. Watch builds compile this to a no-op. */
int rsvm_accel_jit_available(void);
int rsvm_accel_jit_compile(const uint8_t* code, uint16_t len);
int rsvm_accel_jit_run(void);
#else
static inline int rsvm_accel_jit_available(void) { return 0; }
static inline int rsvm_accel_jit_compile(const uint8_t* c, uint16_t n) {
    (void)c; (void)n; return -1;
}
static inline int rsvm_accel_jit_run(void) { return -1; }
#endif

#ifdef __cplusplus
}
#endif
