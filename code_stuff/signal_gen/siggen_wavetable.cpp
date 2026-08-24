#include "siggen_wavetable.h"
#include "precomputed_math/unit_circle_i16.h"

static inline int16_t scale_q15(int16_t s, int16_t amp_q15) {
    return (int16_t)(((int32_t)s * amp_q15) >> 15);
}

void siggen_fill_sine(int16_t* dst, size_t n, uint32_t* phase, uint32_t phase_inc, int16_t amp_q15) {
    uint32_t ph = *phase;
    for (size_t i = 0; i < n; ++i) {
        dst[i] = scale_q15(pcm_sin_i16_lerp(ph), amp_q15);
        ph += phase_inc;
    }
    *phase = ph;
}

void siggen_fill_triangle(int16_t* dst, size_t n, uint32_t* phase, uint32_t phase_inc, int16_t amp_q15) {
    uint32_t ph = *phase;
    for (size_t i = 0; i < n; ++i) {
        // map phase 0..2^32 → triangle -1..1
        uint32_t x = ph >> 16;           // 0..65535
        int32_t t;
        if (x < 32768)
            t = ((int32_t)x * 2) - 32767;          // rising
        else
            t = 32767 - ((int32_t)(x - 32768) * 2); // falling
        dst[i] = scale_q15((int16_t)t, amp_q15);
        ph += phase_inc;
    }
    *phase = ph;
}

void siggen_fill_saw(int16_t* dst, size_t n, uint32_t* phase, uint32_t phase_inc, int16_t amp_q15) {
    uint32_t ph = *phase;
    for (size_t i = 0; i < n; ++i) {
        int16_t s = (int16_t)((ph >> 16) - 32768); // -32768..32767
        dst[i] = scale_q15(s, amp_q15);
        ph += phase_inc;
    }
    *phase = ph;
}

void siggen_fill_square(int16_t* dst, size_t n, uint32_t* phase, uint32_t phase_inc,
                        int16_t amp_q15, uint8_t duty_pct) {
    uint32_t ph = *phase;
    uint32_t thresh = (uint32_t)((duty_pct > 100 ? 100 : duty_pct) * 65535u / 100u) << 16;
    for (size_t i = 0; i < n; ++i) {
        int16_t s = (ph < thresh) ? PCM_Q15_ONE : (int16_t)(-PCM_Q15_ONE);
        dst[i] = scale_q15(s, amp_q15);
        ph += phase_inc;
    }
    *phase = ph;
}

void siggen_fill_noise(int16_t* dst, size_t n, int16_t amp_q15) {
    // xorshift32
    static uint32_t s = 0xA3C5D17Fu;
    for (size_t i = 0; i < n; ++i) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        int16_t raw = (int16_t)(s & 0xFFFF);
        dst[i] = scale_q15(raw, amp_q15);
    }
}

void siggen_fill(siggen_wave_t wave, int16_t* dst, size_t n,
                 uint32_t* phase, uint32_t phase_inc,
                 int16_t amp_q15, uint8_t duty_pct) {
    switch (wave) {
        case SIGGEN_WAVE_SINE:     siggen_fill_sine(dst, n, phase, phase_inc, amp_q15); break;
        case SIGGEN_WAVE_TRIANGLE: siggen_fill_triangle(dst, n, phase, phase_inc, amp_q15); break;
        case SIGGEN_WAVE_SAW:      siggen_fill_saw(dst, n, phase, phase_inc, amp_q15); break;
        case SIGGEN_WAVE_SQUARE:   siggen_fill_square(dst, n, phase, phase_inc, amp_q15, duty_pct); break;
        case SIGGEN_WAVE_NOISE:    siggen_fill_noise(dst, n, amp_q15); break;
        default:                   siggen_fill_sine(dst, n, phase, phase_inc, amp_q15); break;
    }
}
