#include "rsvm_accel.h"

void rsvm_accel_osc_q15(int16_t* dst, uint16_t n, uint8_t phase0, uint8_t step) {
    uint8_t ph = phase0;
    for (uint16_t i = 0; i < n; ++i) {
        dst[i] = rsvm_sin_q15[ph];
        ph = (uint8_t)(ph + step);
    }
}

#if RSVM_ACCEL_HAS_JIT
int rsvm_accel_jit_available(void) { return 0; /* hook; wire AOT-C / gcc later */ }
int rsvm_accel_jit_compile(const uint8_t* code, uint16_t len) {
    (void)code; (void)len;
    return -1;
}
int rsvm_accel_jit_run(void) { return -1; }
#endif
