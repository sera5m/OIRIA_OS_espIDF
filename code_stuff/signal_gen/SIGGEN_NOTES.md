# Signal generator — instruction set + backends

## Goal

**Fire-and-forget** programs: slap formulas together, stack triggers (peak/trough/IO),
route samples out GPIO / LEDC / PDM / **protocol_spi** / **protocol_i2c**. Works from
plain C, C++ fluent API, or later Vulcan `OP_NATIVE`.

Refs: [BojanJurca Esp32_oscilloscope](https://github.com/BojanJurca/Esp32_oscilloscope)
(web scope), [Hackaday browser ESP32 scope](https://hackaday.com/2025/12/21/a-compact-browser-based-esp32-oscilloscope/)
(DMA ADC + test signal). Generator side is this tree; ADC cal is already in `siggen_adc_cal.*`.

## ESP32-S3 outputs

| Path | Use |
|------|-----|
| LEDC PWM | Square / duty-modulated arbitrary + LPF |
| I2S PDM | Analog-ish (no classic DAC on S3) |
| GPIO | Digital from threshold or trigger actions |
| **protocol_spi** | Nested sample stream to external DAC / bus |
| **protocol_i2c** | Nested sample → reg write on codec/DAC |

## Instruction object (`sig_insn`)

Packed ops → `sig_program_t` → `sig_insn_fire()` spawns a runner task and returns.

### C++ (fluent)

```cpp
#include "signal_gen/sig_insn.h"

SigProgram p;
p.rate(2000)
 .sine(/*bus*/0, 1000, 90)
 .out_pdm(17, 0, 48000)
 .on_peak_gpio(0, 2, 1)
 .on_trough_gpio(0, 2, 0)
 .loop();
p.fire();   // autonomous — no further calls needed
```

### Formulas on buses (0..7)

```cpp
p.sine(0, 5000, 80)
 .sine(1, 200, 100)
 .am(/*dst*/2, /*carrier*/0, /*mod*/1)   // c*(1+m)/2
 .protocol_spi(2, /*SPI2*/1, /*cs*/10, 16);
```

Also: `add`, `mul`, `constant`, `triangle`, `square`, `saw`.

### Stacked commands

| Op | Meaning |
|----|---------|
| `on_peak_gpio(bus, pin, level)` | GPIO action when sine-like peak detected |
| `on_trough_gpio(...)` | Same for trough |
| `out_gpio` / `out_ledc` / `out_pdm` | Continuous sinks from a bus |
| `protocol_spi` / `protocol_i2c` | Wire sinks each eval tick |

### C API

```c
sig_program_t prog;
sig_prog_init(&prog);
sig_emit_wave(&prog, 0, SIG_WAVE_SINE, 1000, 90);
sig_emit_out_pdm(&prog, 17, 0, 48000);
sig_emit_on_peak_gpio(&prog, 0, 2, 1);
sig_emit_end(&prog);
sig_insn_fire(&prog);
```

Examples: `sig_insn_examples.hpp`.

## Layout

```
precomputed_math/
  unit_circle_i16.h       Q15 sin/cos/tan LUT + lerp
  fast_inv_trig_i16.h     inv sqrt / atan2 / phase_inc

signal_gen/
  sig_insn.h / .cpp       ISA, builder, runner task
  sig_protocol.h / .cpp   SPI + I2C sample sinks
  sig_insn_examples.hpp   copy-paste recipes
  siggen_wavetable.*      buffer fillers
  siggen_ledc.*           PWM backend
  siggen_i2s_pdm.*        PDM backend
  siggen_adc_cal.*        curve-fitting mV
  siggen_ctrl.*           older singleton API (still valid)
  siggen_types.h
```

## Vulcan path

Same bytecode can be assembled from scripts once `OP_NATIVE` maps:

```text
native("sig_fire", blob_ptr, len)
native("sig_stop")
```

GPIO-only patterns already work via host `pin_mode` / `gpio_wr`.

## CMake

```cmake
"signal_gen/siggen_wavetable.cpp"
"signal_gen/siggen_ledc.cpp"
"signal_gen/siggen_i2s_pdm.cpp"
"signal_gen/siggen_adc_cal.cpp"
"signal_gen/siggen_ctrl.cpp"
"signal_gen/sig_insn.cpp"
"signal_gen/sig_protocol.cpp"
```

Requires: `driver`, `esp_adc`, `esp_driver_i2s`, SPI + I2C master (IDF 5.x).
Include path must reach `precomputed_math/` and `signal_gen/`.
