#include "sig_insn.h"
#include "sig_protocol.h"
#include "siggen_wavetable.h"
#include "siggen_ledc.h"
#include "siggen_i2s_pdm.h"
#include "precomputed_math/unit_circle_i16.h"
#include "precomputed_math/fast_inv_trig_i16.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "sig_insn";

// ---- Emit primitives ---------------------------------------------------------
void sig_prog_init(sig_program_t* p) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
}

esp_err_t sig_prog_emit_u8(sig_program_t* p, uint8_t v) {
    if (!p || p->len + 1 > SIG_INSN_MAX_BYTES) return ESP_ERR_NO_MEM;
    p->code[p->len++] = v;
    return ESP_OK;
}
esp_err_t sig_prog_emit_u16(sig_program_t* p, uint16_t v) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(sig_prog_emit_u8(p, (uint8_t)(v & 0xFF)));
    return sig_prog_emit_u8(p, (uint8_t)(v >> 8));
}
esp_err_t sig_prog_emit_u32(sig_program_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        if (sig_prog_emit_u8(p, (uint8_t)(v >> (8 * i))) != ESP_OK) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
esp_err_t sig_prog_emit_i16(sig_program_t* p, int16_t v) {
    return sig_prog_emit_u16(p, (uint16_t)v);
}

esp_err_t sig_emit_wave(sig_program_t* p, uint8_t bus, sig_wave_kind_t w, uint32_t freq_hz, uint8_t amp_pct) {
    if (bus >= SIG_BUS_SLOTS) return ESP_ERR_INVALID_ARG;
    sig_prog_emit_u8(p, SIG_OP_WAVE);
    sig_prog_emit_u8(p, bus);
    sig_prog_emit_u8(p, (uint8_t)w);
    sig_prog_emit_u32(p, freq_hz);
    return sig_prog_emit_u8(p, amp_pct);
}
esp_err_t sig_emit_const(sig_program_t* p, uint8_t bus, int16_t q15) {
    sig_prog_emit_u8(p, SIG_OP_CONST);
    sig_prog_emit_u8(p, bus);
    return sig_prog_emit_i16(p, q15);
}
esp_err_t sig_emit_add(sig_program_t* p, uint8_t dst, uint8_t a, uint8_t b) {
    sig_prog_emit_u8(p, SIG_OP_ADD);
    sig_prog_emit_u8(p, dst); sig_prog_emit_u8(p, a); return sig_prog_emit_u8(p, b);
}
esp_err_t sig_emit_mul(sig_program_t* p, uint8_t dst, uint8_t a, uint8_t b) {
    sig_prog_emit_u8(p, SIG_OP_MUL);
    sig_prog_emit_u8(p, dst); sig_prog_emit_u8(p, a); return sig_prog_emit_u8(p, b);
}
esp_err_t sig_emit_am(sig_program_t* p, uint8_t dst, uint8_t carrier, uint8_t mod) {
    sig_prog_emit_u8(p, SIG_OP_AM);
    sig_prog_emit_u8(p, dst); sig_prog_emit_u8(p, carrier); return sig_prog_emit_u8(p, mod);
}
esp_err_t sig_emit_out_pdm(sig_program_t* p, uint8_t pin, uint8_t bus, uint32_t sample_rate) {
    sig_prog_emit_u8(p, SIG_OP_OUT_PDM);
    sig_prog_emit_u8(p, pin); sig_prog_emit_u8(p, bus);
    return sig_prog_emit_u32(p, sample_rate);
}
esp_err_t sig_emit_out_ledc(sig_program_t* p, uint8_t pin, uint8_t bus) {
    sig_prog_emit_u8(p, SIG_OP_OUT_LEDC);
    sig_prog_emit_u8(p, pin); return sig_prog_emit_u8(p, bus);
}
esp_err_t sig_emit_out_gpio(sig_program_t* p, uint8_t pin, uint8_t bus, uint8_t thresh_pct) {
    sig_prog_emit_u8(p, SIG_OP_OUT_GPIO);
    sig_prog_emit_u8(p, pin); sig_prog_emit_u8(p, bus); return sig_prog_emit_u8(p, thresh_pct);
}
esp_err_t sig_emit_out_spi(sig_program_t* p, uint8_t bus, uint8_t spi_host, uint8_t cs_pin, uint8_t bits) {
    sig_prog_emit_u8(p, SIG_OP_OUT_SPI);
    sig_prog_emit_u8(p, bus); sig_prog_emit_u8(p, spi_host);
    sig_prog_emit_u8(p, cs_pin); return sig_prog_emit_u8(p, bits);
}
esp_err_t sig_emit_out_i2c(sig_program_t* p, uint8_t bus, uint8_t port, uint8_t addr, uint8_t reg) {
    sig_prog_emit_u8(p, SIG_OP_OUT_I2C);
    sig_prog_emit_u8(p, bus); sig_prog_emit_u8(p, port);
    sig_prog_emit_u8(p, addr); return sig_prog_emit_u8(p, reg);
}
// Trigger action payload: fixed form GPIO_SET pin level after the trigger opcode
esp_err_t sig_emit_on_peak_gpio(sig_program_t* p, uint8_t bus, uint8_t pin, uint8_t level) {
    sig_prog_emit_u8(p, SIG_OP_ON_PEAK);
    sig_prog_emit_u8(p, bus);
    sig_prog_emit_u8(p, SIG_OP_GPIO_SET);
    sig_prog_emit_u8(p, pin);
    return sig_prog_emit_u8(p, level);
}
esp_err_t sig_emit_on_trough_gpio(sig_program_t* p, uint8_t bus, uint8_t pin, uint8_t level) {
    sig_prog_emit_u8(p, SIG_OP_ON_TROUGH);
    sig_prog_emit_u8(p, bus);
    sig_prog_emit_u8(p, SIG_OP_GPIO_SET);
    sig_prog_emit_u8(p, pin);
    return sig_prog_emit_u8(p, level);
}
esp_err_t sig_emit_rate(sig_program_t* p, uint32_t eval_hz) {
    sig_prog_emit_u8(p, SIG_OP_RATE_HZ);
    return sig_prog_emit_u32(p, eval_hz);
}
esp_err_t sig_emit_loop_forever(sig_program_t* p) {
    // marker only — runner loops from 0 until stop
    return sig_prog_emit_u8(p, SIG_OP_LOOP);
}
esp_err_t sig_emit_end(sig_program_t* p) {
    return sig_prog_emit_u8(p, SIG_OP_END);
}

// ---- Runtime -----------------------------------------------------------------
typedef struct {
    uint8_t        kind;      // sig_wave_kind_t or 0xFF = const/noise
    uint32_t       freq_hz;
    uint8_t        amp_pct;
    uint32_t       phase;
    uint32_t       phase_inc;
    int16_t        const_q15;
    int16_t        value;     // last sample
    int16_t        prev;      // for edge detect
    bool           active;
} bus_slot_t;

typedef struct {
    uint8_t type;             // SIG_OP_ON_PEAK / TROUGH / RISE / FALL
    uint8_t bus;
    uint8_t action;           // SIG_OP_GPIO_SET
    uint8_t pin;
    uint8_t level;
    bool    armed;            // edge latch
} trigger_t;

typedef struct {
    uint8_t kind;             // OUT_*
    uint8_t bus;
    uint8_t pin;
    uint8_t thresh;
    uint8_t spi_host, cs, bits;
    uint8_t i2c_port, addr, reg;
    uint32_t sample_rate;
    sig_spi_sink_t spi;
    sig_i2c_sink_t i2c;
    siggen_state_t ledc_st;
    siggen_state_t pdm_st;
    bool spi_ok, i2c_ok, ledc_ok, pdm_ok;
} out_sink_t;

#define MAX_TRIG  8
#define MAX_OUT   4

typedef struct {
    sig_program_t* prog;
    bus_slot_t     bus[SIG_BUS_SLOTS];
    trigger_t      trig[MAX_TRIG];
    int            n_trig;
    out_sink_t     out[MAX_OUT];
    int            n_out;
    uint32_t       eval_hz;
    bool           loop;
} run_ctx_t;

static uint8_t rd_u8(const uint8_t* c, uint16_t* pc) { return c[(*pc)++]; }
static uint16_t rd_u16(const uint8_t* c, uint16_t* pc) {
    uint16_t v = c[*pc] | ((uint16_t)c[*pc + 1] << 8);
    *pc += 2; return v;
}
static uint32_t rd_u32(const uint8_t* c, uint16_t* pc) {
    uint32_t v = (uint32_t)c[*pc] | ((uint32_t)c[*pc+1]<<8) |
                 ((uint32_t)c[*pc+2]<<16) | ((uint32_t)c[*pc+3]<<24);
    *pc += 4; return v;
}

// Pass 1: scan program, register waves / outs / triggers (no sample loop yet)
static void decode_setup(run_ctx_t* cx) {
    const uint8_t* c = cx->prog->code;
    uint16_t pc = 0;
    uint16_t len = cx->prog->len;
    cx->eval_hz = 1000;
    cx->loop = true;

    while (pc < len) {
        uint8_t op = rd_u8(c, &pc);
        switch (op) {
        case SIG_OP_END:
            return;
        case SIG_OP_NOP:
            break;
        case SIG_OP_WAVE: {
            uint8_t bus = rd_u8(c, &pc);
            uint8_t w = rd_u8(c, &pc);
            uint32_t f = rd_u32(c, &pc);
            uint8_t amp = rd_u8(c, &pc);
            if (bus < SIG_BUS_SLOTS) {
                cx->bus[bus].kind = w;
                cx->bus[bus].freq_hz = f;
                cx->bus[bus].amp_pct = amp;
                cx->bus[bus].phase = 0;
                cx->bus[bus].phase_inc = pcm_phase_inc(f, cx->eval_hz);
                cx->bus[bus].active = true;
            }
            break;
        }
        case SIG_OP_CONST: {
            uint8_t bus = rd_u8(c, &pc);
            int16_t v = (int16_t)rd_u16(c, &pc);
            if (bus < SIG_BUS_SLOTS) {
                cx->bus[bus].kind = 0xFE;
                cx->bus[bus].const_q15 = v;
                cx->bus[bus].value = v;
                cx->bus[bus].active = true;
            }
            break;
        }
        case SIG_OP_NOISE: {
            uint8_t bus = rd_u8(c, &pc);
            uint8_t amp = rd_u8(c, &pc);
            if (bus < SIG_BUS_SLOTS) {
                cx->bus[bus].kind = 0xFD;
                cx->bus[bus].amp_pct = amp;
                cx->bus[bus].active = true;
            }
            break;
        }
        case SIG_OP_ADD:
        case SIG_OP_SUB:
        case SIG_OP_MUL:
        case SIG_OP_AM:
            pc += 3; // dst,a,b — applied each tick
            break;
        case SIG_OP_SCALE:
            pc += 3;
            break;
        case SIG_OP_FM_PHASE:
            pc += 4;
            break;
        case SIG_OP_OUT_GPIO: {
            if (cx->n_out >= MAX_OUT) { pc += 3; break; }
            out_sink_t* o = &cx->out[cx->n_out++];
            memset(o, 0, sizeof(*o));
            o->kind = SIG_OP_OUT_GPIO;
            o->pin = rd_u8(c, &pc);
            o->bus = rd_u8(c, &pc);
            o->thresh = rd_u8(c, &pc);
            gpio_config_t io = {};
            io.pin_bit_mask = 1ULL << o->pin;
            io.mode = GPIO_MODE_OUTPUT;
            gpio_config(&io);
            break;
        }
        case SIG_OP_OUT_LEDC: {
            if (cx->n_out >= MAX_OUT) { pc += 2; break; }
            out_sink_t* o = &cx->out[cx->n_out++];
            memset(o, 0, sizeof(*o));
            o->kind = SIG_OP_OUT_LEDC;
            o->pin = rd_u8(c, &pc);
            o->bus = rd_u8(c, &pc);
            o->ledc_st.cfg.wave = SIGGEN_WAVE_SINE;
            o->ledc_st.cfg.out = SIGGEN_OUT_LEDC;
            o->ledc_st.cfg.gpio = o->pin;
            o->ledc_st.cfg.freq_hz = 20000; // carrier for duty-mod
            o->ledc_st.cfg.duty_percent = 50;
            o->ledc_ok = (siggen_ledc_init(&o->ledc_st) == ESP_OK);
            if (o->ledc_ok) siggen_ledc_start(&o->ledc_st);
            break;
        }
        case SIG_OP_OUT_PDM: {
            if (cx->n_out >= MAX_OUT) { pc += 6; break; }
            out_sink_t* o = &cx->out[cx->n_out++];
            memset(o, 0, sizeof(*o));
            o->kind = SIG_OP_OUT_PDM;
            o->pin = rd_u8(c, &pc);
            o->bus = rd_u8(c, &pc);
            o->sample_rate = rd_u32(c, &pc);
            o->pdm_st.cfg.wave = SIGGEN_WAVE_SINE;
            o->pdm_st.cfg.out = SIGGEN_OUT_I2S_PDM;
            o->pdm_st.cfg.gpio = o->pin;
            o->pdm_st.cfg.sample_rate = o->sample_rate ? o->sample_rate : 48000;
            o->pdm_st.cfg.freq_hz = 1000;
            o->pdm_st.cfg.amplitude = 80;
            o->pdm_ok = (siggen_i2s_pdm_init(&o->pdm_st) == ESP_OK);
            if (o->pdm_ok) siggen_i2s_pdm_start(&o->pdm_st);
            break;
        }
        case SIG_OP_OUT_SPI: {
            if (cx->n_out >= MAX_OUT) { pc += 4; break; }
            out_sink_t* o = &cx->out[cx->n_out++];
            memset(o, 0, sizeof(*o));
            o->kind = SIG_OP_OUT_SPI;
            o->bus = rd_u8(c, &pc);
            o->spi_host = rd_u8(c, &pc);
            o->cs = rd_u8(c, &pc);
            o->bits = rd_u8(c, &pc);
            // Default MOSI/SCLK — caller should have bus pins set in board wiring;
            // use -1 to avoid re-binding if bus already up.
            o->spi_ok = (sig_spi_sink_init(&o->spi, o->spi_host, o->cs, -1, -1, 1000000, o->bits) == ESP_OK);
            break;
        }
        case SIG_OP_OUT_I2C: {
            if (cx->n_out >= MAX_OUT) { pc += 4; break; }
            out_sink_t* o = &cx->out[cx->n_out++];
            memset(o, 0, sizeof(*o));
            o->kind = SIG_OP_OUT_I2C;
            o->bus = rd_u8(c, &pc);
            o->i2c_port = rd_u8(c, &pc);
            o->addr = rd_u8(c, &pc);
            o->reg = rd_u8(c, &pc);
            // Pins must be provided by board layer — use common defaults 8/9 if unset
            o->i2c_ok = (sig_i2c_sink_init(&o->i2c, o->i2c_port, 8, 9, o->addr, o->reg, 400000) == ESP_OK);
            break;
        }
        case SIG_OP_ON_PEAK:
        case SIG_OP_ON_TROUGH:
        case SIG_OP_ON_RISE:
        case SIG_OP_ON_FALL: {
            if (cx->n_trig >= MAX_TRIG) { pc += 4; break; }
            trigger_t* t = &cx->trig[cx->n_trig++];
            t->type = op;
            t->bus = rd_u8(c, &pc);
            t->action = rd_u8(c, &pc);
            t->pin = rd_u8(c, &pc);
            t->level = rd_u8(c, &pc);
            t->armed = true;
            if (t->action == SIG_OP_GPIO_SET) {
                gpio_config_t io = {};
                io.pin_bit_mask = 1ULL << t->pin;
                io.mode = GPIO_MODE_OUTPUT;
                gpio_config(&io);
            }
            break;
        }
        case SIG_OP_GPIO_SET: {
            uint8_t pin = rd_u8(c, &pc);
            uint8_t lvl = rd_u8(c, &pc);
            gpio_config_t io = {};
            io.pin_bit_mask = 1ULL << pin;
            io.mode = GPIO_MODE_OUTPUT;
            gpio_config(&io);
            gpio_set_level((gpio_num_t)pin, lvl ? 1 : 0);
            break;
        }
        case SIG_OP_GPIO_TOG:
            pc += 1;
            break;
        case SIG_OP_WAIT_MS:
            pc += 2;
            break;
        case SIG_OP_LOOP:
            cx->loop = true;
            break;
        case SIG_OP_RATE_HZ: {
            cx->eval_hz = rd_u32(c, &pc);
            if (cx->eval_hz == 0) cx->eval_hz = 1000;
            // fix phase_inc for already-registered waves
            for (int i = 0; i < SIG_BUS_SLOTS; ++i) {
                if (cx->bus[i].active && cx->bus[i].kind <= SIG_WAVE_SAW)
                    cx->bus[i].phase_inc = pcm_phase_inc(cx->bus[i].freq_hz, cx->eval_hz);
            }
            break;
        }
        default:
            ESP_LOGW(TAG, "unknown op 0x%02x at %u", op, (unsigned)(pc - 1));
            return;
        }
    }
}

static int16_t sample_wave(bus_slot_t* s) {
    int16_t amp = (int16_t)((s->amp_pct > 100 ? 100 : s->amp_pct) * 32767 / 100);
    int16_t out = 0;
    switch (s->kind) {
    case SIG_WAVE_SINE:
        out = (int16_t)(((int32_t)pcm_sin_i16_lerp(s->phase) * amp) >> 15);
        s->phase += s->phase_inc;
        break;
    case SIG_WAVE_SQUARE: {
        int16_t raw = (s->phase < 0x80000000u) ? 32767 : -32768;
        out = (int16_t)(((int32_t)raw * amp) >> 15);
        s->phase += s->phase_inc;
        break;
    }
    case SIG_WAVE_TRIANGLE: {
        uint32_t x = s->phase >> 16;
        int32_t t = (x < 32768) ? ((int32_t)x * 2 - 32767) : (32767 - (int32_t)(x - 32768) * 2);
        out = (int16_t)((t * amp) >> 15);
        s->phase += s->phase_inc;
        break;
    }
    case SIG_WAVE_SAW: {
        int16_t raw = (int16_t)((s->phase >> 16) - 32768);
        out = (int16_t)(((int32_t)raw * amp) >> 15);
        s->phase += s->phase_inc;
        break;
    }
    case 0xFE:
        out = s->const_q15;
        break;
    case 0xFD: {
        static uint32_t rng = 0xBADC0FFEu;
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        out = (int16_t)((((int32_t)(int16_t)(rng & 0xFFFF)) * amp) >> 15);
        break;
    }
    default:
        out = 0;
        break;
    }
    s->prev = s->value;
    s->value = out;
    return out;
}

// Apply formula ops in program order each tick (single pass over code)
static void apply_formulas(run_ctx_t* cx) {
    const uint8_t* c = cx->prog->code;
    uint16_t pc = 0;
    uint16_t len = cx->prog->len;
    while (pc < len) {
        uint8_t op = rd_u8(c, &pc);
        switch (op) {
        case SIG_OP_END: return;
        case SIG_OP_WAVE: pc += 1+1+4+1; break;
        case SIG_OP_CONST: pc += 1+2; break;
        case SIG_OP_NOISE: pc += 2; break;
        case SIG_OP_ADD: {
            uint8_t d = rd_u8(c,&pc), a = rd_u8(c,&pc), b = rd_u8(c,&pc);
            if (d < SIG_BUS_SLOTS && a < SIG_BUS_SLOTS && b < SIG_BUS_SLOTS) {
                int32_t s = (int32_t)cx->bus[a].value + cx->bus[b].value;
                if (s > 32767) s = 32767;
                if (s < -32768) s = -32768;
                cx->bus[d].prev = cx->bus[d].value;
                cx->bus[d].value = (int16_t)s;
                cx->bus[d].active = true;
            }
            break;
        }
        case SIG_OP_SUB: {
            uint8_t d = rd_u8(c,&pc), a = rd_u8(c,&pc), b = rd_u8(c,&pc);
            if (d < SIG_BUS_SLOTS && a < SIG_BUS_SLOTS && b < SIG_BUS_SLOTS) {
                int32_t s = (int32_t)cx->bus[a].value - cx->bus[b].value;
                if (s > 32767) s = 32767;
                if (s < -32768) s = -32768;
                cx->bus[d].prev = cx->bus[d].value;
                cx->bus[d].value = (int16_t)s;
                cx->bus[d].active = true;
            }
            break;
        }
        case SIG_OP_MUL: {
            uint8_t d = rd_u8(c,&pc), a = rd_u8(c,&pc), b = rd_u8(c,&pc);
            if (d < SIG_BUS_SLOTS && a < SIG_BUS_SLOTS && b < SIG_BUS_SLOTS) {
                int32_t s = ((int32_t)cx->bus[a].value * cx->bus[b].value) >> 15;
                cx->bus[d].prev = cx->bus[d].value;
                cx->bus[d].value = (int16_t)s;
                cx->bus[d].active = true;
            }
            break;
        }
        case SIG_OP_AM: {
            uint8_t d = rd_u8(c,&pc), car = rd_u8(c,&pc), mod = rd_u8(c,&pc);
            if (d < SIG_BUS_SLOTS && car < SIG_BUS_SLOTS && mod < SIG_BUS_SLOTS) {
                // c * (1 + m) / 2   with m,c in Q15
                int32_t m = cx->bus[mod].value;
                int32_t scale = (32767 + m) >> 1;
                int32_t s = ((int32_t)cx->bus[car].value * scale) >> 15;
                cx->bus[d].prev = cx->bus[d].value;
                cx->bus[d].value = (int16_t)s;
                cx->bus[d].active = true;
            }
            break;
        }
        case SIG_OP_SCALE: pc += 3; break;
        case SIG_OP_FM_PHASE: pc += 4; break;
        case SIG_OP_OUT_GPIO: pc += 3; break;
        case SIG_OP_OUT_LEDC: pc += 2; break;
        case SIG_OP_OUT_PDM: pc += 6; break;
        case SIG_OP_OUT_SPI: pc += 4; break;
        case SIG_OP_OUT_I2C: pc += 4; break;
        case SIG_OP_ON_PEAK: case SIG_OP_ON_TROUGH:
        case SIG_OP_ON_RISE: case SIG_OP_ON_FALL: pc += 4; break;
        case SIG_OP_GPIO_SET: pc += 2; break;
        case SIG_OP_GPIO_TOG: pc += 1; break;
        case SIG_OP_WAIT_MS: pc += 2; break;
        case SIG_OP_LOOP: break;
        case SIG_OP_RATE_HZ: pc += 4; break;
        default: return;
        }
    }
}

static void fire_triggers(run_ctx_t* cx) {
    for (int i = 0; i < cx->n_trig; ++i) {
        trigger_t* t = &cx->trig[i];
        if (t->bus >= SIG_BUS_SLOTS) continue;
        bus_slot_t* b = &cx->bus[t->bus];
        bool fire = false;
        switch (t->type) {
        case SIG_OP_ON_PEAK:
            // prev rising, now falling through local max
            fire = (b->prev < b->value) ? false :
                   (b->prev > 0 && b->value <= b->prev && b->prev > 0);
            // simpler: zero-cross of derivative positive→negative near top third
            if (b->prev > 16000 && b->value < b->prev && b->prev >= b->value)
                fire = true;
            break;
        case SIG_OP_ON_TROUGH:
            if (b->prev < -16000 && b->value > b->prev)
                fire = true;
            break;
        case SIG_OP_ON_RISE:
            fire = (b->prev < 0 && b->value >= 0);
            break;
        case SIG_OP_ON_FALL:
            fire = (b->prev >= 0 && b->value < 0);
            break;
        }
        if (fire && t->action == SIG_OP_GPIO_SET) {
            gpio_set_level((gpio_num_t)t->pin, t->level ? 1 : 0);
        }
    }
}

static void push_outputs(run_ctx_t* cx) {
    for (int i = 0; i < cx->n_out; ++i) {
        out_sink_t* o = &cx->out[i];
        if (o->bus >= SIG_BUS_SLOTS) continue;
        int16_t s = cx->bus[o->bus].value;
        switch (o->kind) {
        case SIG_OP_OUT_GPIO: {
            int thr = ((int)o->thresh * 65535 / 100) - 32768;
            gpio_set_level((gpio_num_t)o->pin, s >= thr ? 1 : 0);
            break;
        }
        case SIG_OP_OUT_LEDC:
            if (o->ledc_ok) siggen_ledc_apply_sample(&o->ledc_st, s);
            break;
        case SIG_OP_OUT_PDM:
            if (o->pdm_ok) siggen_i2s_pdm_write(&o->pdm_st, &s, 1);
            break;
        case SIG_OP_OUT_SPI:
            if (o->spi_ok) sig_spi_sink_write_sample(&o->spi, s);
            break;
        case SIG_OP_OUT_I2C:
            if (o->i2c_ok) sig_i2c_sink_write_sample(&o->i2c, s);
            break;
        }
    }
}

static void runner_task(void* arg) {
    run_ctx_t* cx = (run_ctx_t*)arg;
    decode_setup(cx);
    ESP_LOGI(TAG, "runner start eval=%lu Hz outs=%d trigs=%d",
             (unsigned long)cx->eval_hz, cx->n_out, cx->n_trig);

    TickType_t period = pdMS_TO_TICKS(1000 / (cx->eval_hz ? cx->eval_hz : 1000));
    if (period == 0) period = 1;
    TickType_t last = xTaskGetTickCount();

    while (cx->prog->running) {
        // 1) sample all wave sources
        for (int i = 0; i < SIG_BUS_SLOTS; ++i) {
            if (cx->bus[i].active && cx->bus[i].kind != 0xFE)
                sample_wave(&cx->bus[i]);
            else if (cx->bus[i].active && cx->bus[i].kind == 0xFE)
                cx->bus[i].value = cx->bus[i].const_q15;
        }
        // 2) formulas
        apply_formulas(cx);
        // 3) triggers
        fire_triggers(cx);
        // 4) sinks
        push_outputs(cx);

        vTaskDelayUntil(&last, period);
    }

    // teardown sinks
    for (int i = 0; i < cx->n_out; ++i) {
        out_sink_t* o = &cx->out[i];
        if (o->spi_ok) sig_spi_sink_deinit(&o->spi);
        if (o->i2c_ok) sig_i2c_sink_deinit(&o->i2c);
        if (o->ledc_ok) siggen_ledc_stop(&o->ledc_st);
        if (o->pdm_ok) siggen_i2s_pdm_stop(&o->pdm_st);
    }
    ESP_LOGI(TAG, "runner stop");
    free(cx);
    vTaskDelete(NULL);
}

esp_err_t sig_insn_fire(sig_program_t* p) {
    if (!p || p->len == 0) return ESP_ERR_INVALID_ARG;
    if (p->running) return ESP_ERR_INVALID_STATE;
    run_ctx_t* cx = (run_ctx_t*)calloc(1, sizeof(run_ctx_t));
    if (!cx) return ESP_ERR_NO_MEM;
    cx->prog = p;
    p->running = true;
    BaseType_t ok = xTaskCreate(runner_task, "sig_insn", 6144, cx, 5, (TaskHandle_t*)&p->task);
    if (ok != pdPASS) {
        p->running = false;
        free(cx);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t sig_insn_stop(sig_program_t* p) {
    if (!p) return ESP_ERR_INVALID_ARG;
    p->running = false;
    return ESP_OK;
}
