#pragma once
// Drop-in usage sketches — fire and forget
#include "sig_insn.h"

// 1 kHz sine on PDM GPIO17; GPIO2 high on peaks, low on troughs
inline esp_err_t example_sine_peak_gpio() {
    SigProgram p;
    p.rate(2000)
     .sine(/*bus*/0, /*hz*/1000, /*amp*/90)
     .out_pdm(/*pin*/17, /*bus*/0, 48000)
     .on_peak_gpio(0, 2, 1)
     .on_trough_gpio(0, 2, 0)
     .loop();
    return p.fire();
}

// AM: carrier bus0 5 kHz, mod bus1 200 Hz → bus2 → SPI DAC
inline esp_err_t example_am_spi() {
    SigProgram p;
    p.rate(5000)
     .sine(0, 5000, 80)
     .sine(1, 200, 100)
     .am(/*dst*/2, /*car*/0, /*mod*/1)
     .protocol_spi(/*bus*/2, /*host*/1, /*cs*/10, /*bits*/16)
     .loop();
    return p.fire();
}

// Stack: triangle + square sum → LEDC duty; I2C codec reg; rise edge blink
inline esp_err_t example_stack_formulas() {
    SigProgram p;
    p.rate(1000)
     .triangle(0, 400, 70)
     .square(1, 400, 40)
     .add(2, 0, 1)
     .out_ledc(18, 2)
     .protocol_i2c(/*bus*/2, /*port*/0, /*addr*/0x18, /*reg*/0x01)
     .on_peak_gpio(2, 4, 1)
     .on_trough_gpio(2, 4, 0)
     .loop();
    return p.fire();
}

// Pure C (no C++ class) — same sine + peak
inline esp_err_t example_c_api() {
    sig_program_t prog;
    sig_prog_init(&prog);
    sig_emit_rate(&prog, 2000);
    sig_emit_wave(&prog, 0, SIG_WAVE_SINE, 1000, 90);
    sig_emit_out_pdm(&prog, 17, 0, 48000);
    sig_emit_on_peak_gpio(&prog, 0, 2, 1);
    sig_emit_on_trough_gpio(&prog, 0, 2, 0);
    sig_emit_loop_forever(&prog);
    sig_emit_end(&prog);
    return sig_insn_fire(&prog);
}
