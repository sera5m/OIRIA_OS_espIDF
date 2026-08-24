#pragma once
// =============================================================================
// Signal generator — shared types (primary or slave ESP32-S3)
// =============================================================================
// ESP32-S3 has NO classic DAC. Output paths:
//   SIGGEN_OUT_LEDC   — hardware PWM (square; duty-modulated ≈ arbitrary + LPF)
//   SIGGEN_OUT_I2S_PDM— I2S PDM (1-bit density; RC LPF → analog-ish)
//   SIGGEN_OUT_GPIO   — bit-bang / RMT digital patterns via Vulcan GPIO host
//
// Vulcan can drive this through OP_NATIVE / pin_mode / gpio_wr or a thin
// native table ("siggen_start", "siggen_stop", …) registered on the host.
// =============================================================================

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SIGGEN_WAVE_SINE = 0,
    SIGGEN_WAVE_SQUARE,
    SIGGEN_WAVE_TRIANGLE,
    SIGGEN_WAVE_SAW,
    SIGGEN_WAVE_NOISE,
} siggen_wave_t;

typedef enum {
    SIGGEN_OUT_LEDC = 0,
    SIGGEN_OUT_I2S_PDM,
    SIGGEN_OUT_GPIO,
} siggen_out_t;

typedef struct {
    siggen_wave_t wave;
    siggen_out_t  out;
    int           gpio;           // LEDC / GPIO / PDM data pin
    uint32_t      freq_hz;        // 1 .. ~100000 (LEDC); PDM limited by sample rate
    uint8_t       duty_percent;   // square / PWM 0..100
    uint8_t       amplitude;      // 0..100 (% of full scale for sine/tri/saw)
    uint32_t      sample_rate;    // DDS / PDM sample rate (e.g. 48000)
    bool          running;
} siggen_cfg_t;

typedef struct {
    siggen_cfg_t  cfg;
    uint32_t      phase;          // DDS phase accumulator
    uint32_t      phase_inc;
    // backend handles (opaque-ish)
    int           ledc_channel;   // -1 if unused
    int           ledc_timer;
    void*         i2s_tx;         // i2s_chan_handle_t when used
    bool          inited;
} siggen_state_t;

#ifdef __cplusplus
}
#endif
