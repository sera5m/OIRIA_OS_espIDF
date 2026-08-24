#pragma once
// =============================================================================
// sig_insn — fire-and-forget signal instruction set (C/C++ + Vulcan-friendly)
// =============================================================================
// Build a small program of packed ops, call sig_insn_fire(), walk away.
// Runner task evaluates formulas, watches peak/trough, drives GPIO / PDM /
// LEDC / protocol_spi / protocol_i2c without further caller involvement.
//
// Direct C++:
//   SigProgram p; p.sine(1000,80).out_pdm(17).on_peak_gpio(2,1).on_trough_gpio(2,0).fire();
//
// Vulcan / OP_NATIVE later maps names → same ops (push program blob + fire).
// =============================================================================

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Instruction opcodes (u8) ------------------------------------------------
typedef enum {
    SIG_OP_END        = 0x00,  // halt program (stop runner when reached in non-loop)
    SIG_OP_NOP        = 0x01,

    // Sources → named bus slot 0..7
    SIG_OP_WAVE       = 0x10,  // u8 bus, u8 wave, u32 freq_hz, u8 amp_pct
    SIG_OP_CONST      = 0x11,  // u8 bus, i16 q15 value
    SIG_OP_NOISE      = 0x12,  // u8 bus, u8 amp_pct

    // Formulas (result → bus_dst)
    SIG_OP_ADD        = 0x20,  // u8 dst, u8 a, u8 b
    SIG_OP_SUB        = 0x21,
    SIG_OP_MUL        = 0x22,  // Q15 * Q15 → Q15
    SIG_OP_SCALE      = 0x23,  // u8 dst, u8 src, u8 amp_pct
    SIG_OP_AM         = 0x24,  // u8 dst, u8 carrier, u8 mod   (c * (1+m)/2)
    SIG_OP_FM_PHASE   = 0x25,  // u8 dst_wave_bus, u8 mod_bus, u16 depth_q8  (phase mod depth)

    // Outputs (sample from bus each tick, or digital action)
    SIG_OP_OUT_GPIO   = 0x30,  // u8 pin, u8 bus, u8 thresh_pct  (digital from sample)
    SIG_OP_OUT_LEDC   = 0x31,  // u8 pin, u8 bus                 (duty from sample)
    SIG_OP_OUT_PDM    = 0x32,  // u8 pin, u8 bus, u32 sample_rate
    SIG_OP_OUT_SPI    = 0x33,  // u8 bus, u8 spi_host, u8 cs_pin, u8 bits  (protocol_spi)
    SIG_OP_OUT_I2C    = 0x34,  // u8 bus, u8 port, u8 addr, u8 reg         (protocol_i2c)

    // Triggers / stacked actions
    SIG_OP_ON_PEAK    = 0x40,  // u8 bus, u8 action_op, …action payload (see below)
    SIG_OP_ON_TROUGH  = 0x41,
    SIG_OP_ON_RISE    = 0x42,  // zero-cross rising
    SIG_OP_ON_FALL    = 0x43,
    SIG_OP_GPIO_SET   = 0x44,  // u8 pin, u8 level          (immediate or as action)
    SIG_OP_GPIO_TOG   = 0x45,  // u8 pin

    // Control flow
    SIG_OP_WAIT_MS    = 0x50,  // u16 ms
    SIG_OP_LOOP       = 0x51,  // u16 count (0 = forever), i16 rel_back
    SIG_OP_RATE_HZ    = 0x52,  // u32 eval_rate_hz  (default 1000)
} sig_opcode_t;

// Wave kinds (match earlier siggen_wave_t numbering where possible)
typedef enum {
    SIG_WAVE_SINE = 0,
    SIG_WAVE_SQUARE,
    SIG_WAVE_TRIANGLE,
    SIG_WAVE_SAW,
} sig_wave_kind_t;

#define SIG_INSN_MAX_BYTES  512
#define SIG_BUS_SLOTS         8

typedef struct {
    uint8_t  code[SIG_INSN_MAX_BYTES];
    uint16_t len;
    bool     running;
    void*    task;   // TaskHandle_t
} sig_program_t;

// ---- Builder (C API) ---------------------------------------------------------
void     sig_prog_init(sig_program_t* p);
esp_err_t sig_prog_emit_u8(sig_program_t* p, uint8_t v);
esp_err_t sig_prog_emit_u16(sig_program_t* p, uint16_t v);
esp_err_t sig_prog_emit_u32(sig_program_t* p, uint32_t v);
esp_err_t sig_prog_emit_i16(sig_program_t* p, int16_t v);

// High-level emit helpers
esp_err_t sig_emit_wave(sig_program_t* p, uint8_t bus, sig_wave_kind_t w, uint32_t freq_hz, uint8_t amp_pct);
esp_err_t sig_emit_const(sig_program_t* p, uint8_t bus, int16_t q15);
esp_err_t sig_emit_add(sig_program_t* p, uint8_t dst, uint8_t a, uint8_t b);
esp_err_t sig_emit_mul(sig_program_t* p, uint8_t dst, uint8_t a, uint8_t b);
esp_err_t sig_emit_am(sig_program_t* p, uint8_t dst, uint8_t carrier, uint8_t mod);
esp_err_t sig_emit_out_pdm(sig_program_t* p, uint8_t pin, uint8_t bus, uint32_t sample_rate);
esp_err_t sig_emit_out_ledc(sig_program_t* p, uint8_t pin, uint8_t bus);
esp_err_t sig_emit_out_gpio(sig_program_t* p, uint8_t pin, uint8_t bus, uint8_t thresh_pct);
esp_err_t sig_emit_out_spi(sig_program_t* p, uint8_t bus, uint8_t spi_host, uint8_t cs_pin, uint8_t bits);
esp_err_t sig_emit_out_i2c(sig_program_t* p, uint8_t bus, uint8_t port, uint8_t addr, uint8_t reg);
// Trigger: on peak/trough of bus, set GPIO pin to level (stacked action)
esp_err_t sig_emit_on_peak_gpio(sig_program_t* p, uint8_t bus, uint8_t pin, uint8_t level);
esp_err_t sig_emit_on_trough_gpio(sig_program_t* p, uint8_t bus, uint8_t pin, uint8_t level);
esp_err_t sig_emit_rate(sig_program_t* p, uint32_t eval_hz);
esp_err_t sig_emit_loop_forever(sig_program_t* p);  // ends program with infinite loop from start
esp_err_t sig_emit_end(sig_program_t* p);

// Fire-and-forget: spawn runner task, returns immediately
esp_err_t sig_insn_fire(sig_program_t* p);
esp_err_t sig_insn_stop(sig_program_t* p);

#ifdef __cplusplus
}

// ---- C++ fluent builder ------------------------------------------------------
class SigProgram {
public:
    SigProgram() { sig_prog_init(&p_); }

    SigProgram& sine(uint8_t bus, uint32_t hz, uint8_t amp = 80) {
        sig_emit_wave(&p_, bus, SIG_WAVE_SINE, hz, amp); return *this;
    }
    SigProgram& square(uint8_t bus, uint32_t hz, uint8_t amp = 80) {
        sig_emit_wave(&p_, bus, SIG_WAVE_SQUARE, hz, amp); return *this;
    }
    SigProgram& triangle(uint8_t bus, uint32_t hz, uint8_t amp = 80) {
        sig_emit_wave(&p_, bus, SIG_WAVE_TRIANGLE, hz, amp); return *this;
    }
    SigProgram& saw(uint8_t bus, uint32_t hz, uint8_t amp = 80) {
        sig_emit_wave(&p_, bus, SIG_WAVE_SAW, hz, amp); return *this;
    }
    SigProgram& constant(uint8_t bus, int16_t q15) {
        sig_emit_const(&p_, bus, q15); return *this;
    }
    SigProgram& add(uint8_t dst, uint8_t a, uint8_t b) {
        sig_emit_add(&p_, dst, a, b); return *this;
    }
    SigProgram& mul(uint8_t dst, uint8_t a, uint8_t b) {
        sig_emit_mul(&p_, dst, a, b); return *this;
    }
    SigProgram& am(uint8_t dst, uint8_t carrier, uint8_t mod) {
        sig_emit_am(&p_, dst, carrier, mod); return *this;
    }
    SigProgram& out_pdm(uint8_t pin, uint8_t bus = 0, uint32_t sr = 48000) {
        sig_emit_out_pdm(&p_, pin, bus, sr); return *this;
    }
    SigProgram& out_ledc(uint8_t pin, uint8_t bus = 0) {
        sig_emit_out_ledc(&p_, pin, bus); return *this;
    }
    SigProgram& out_gpio(uint8_t pin, uint8_t bus = 0, uint8_t thr = 50) {
        sig_emit_out_gpio(&p_, pin, bus, thr); return *this;
    }
    // protocol sinks — nest signal bytes onto wire
    SigProgram& protocol_spi(uint8_t bus, uint8_t host, uint8_t cs, uint8_t bits = 16) {
        sig_emit_out_spi(&p_, bus, host, cs, bits); return *this;
    }
    SigProgram& protocol_i2c(uint8_t bus, uint8_t port, uint8_t addr, uint8_t reg) {
        sig_emit_out_i2c(&p_, bus, port, addr, reg); return *this;
    }
    SigProgram& on_peak_gpio(uint8_t bus, uint8_t pin, uint8_t level = 1) {
        sig_emit_on_peak_gpio(&p_, bus, pin, level); return *this;
    }
    SigProgram& on_trough_gpio(uint8_t bus, uint8_t pin, uint8_t level = 0) {
        sig_emit_on_trough_gpio(&p_, bus, pin, level); return *this;
    }
    SigProgram& rate(uint32_t hz) { sig_emit_rate(&p_, hz); return *this; }
    SigProgram& loop() { sig_emit_loop_forever(&p_); return *this; }

    // Autonomous: start FreeRTOS task and return
    esp_err_t fire() {
        sig_emit_end(&p_);
        return sig_insn_fire(&p_);
    }
    esp_err_t stop() { return sig_insn_stop(&p_); }

    sig_program_t* raw() { return &p_; }

private:
    sig_program_t p_;
};
#endif
