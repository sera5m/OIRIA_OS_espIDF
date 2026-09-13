# Signal generator — instruction set + backends

## Goal

**Fire-and-forget** programs: slap formulas together, stack triggers (peak/trough/IO),
route samples out GPIO / LEDC / PDM / **protocol_spi** / **protocol_i2c**. Works from
plain C, C++ fluent API, or Vulcan `native("wave", …)`.

Refs: [BojanJurca Esp32_oscilloscope](https://github.com/BojanJurca/Esp32_oscilloscope)
(web scope), [corz.org ESP32 signal generator](https://corz.org/ESP32/square-sine-triangle-wave-signal-generator/)
(web + serial one-letter commands — ported as Vulcan, PWM-only on S3).

## ESP32-S3 outputs

| Path | Use |
|------|-----|
| LEDC PWM | Square at hardware freq; sine/tri/saw = duty DDS + **RC LPF** |
| I2S PDM | Analog-ish (no classic DAC on S3) |
| GPIO | Digital from threshold or trigger actions |
| **protocol_spi** | Nested sample stream to external DAC / bus |
| **protocol_i2c** | Nested sample → reg write on codec/DAC |

## Vulcan / web (this is the user-facing path)

See repo-root `SIGGEN.md`.

```
native("wave", kind, hz, duty, pin, amp)   // 0 sine 1 square 2 tri 3 saw
native("wave_stop")
native("sweep", f0, f1, ms)
native("adc", pin)
```

Web console (STA or AP `OIRIA-vulcan`): drop `.vul`, Corz `s/r/t/2k/p25/stop`, scope canvas.

`siggen_play.*` is the singleton used by both HTTP and `OP_NATIVE`. Same binary on
head (tyrant/solo) and secondary (puppet). Tyrant also UART-forwards `wave` to the puppet.

## Instruction object (`sig_insn`) — lower level / C++

Packed ops → `sig_program_t` → `sig_insn_fire()` spawns a runner task and returns.

```cpp
SigProgram p;
p.rate(2000)
 .sine(/*bus*/0, 1000, 90)
 .out_ledc(4, 0)
 .loop();
p.fire();
```

## Layout

```
precomputed_math/
  unit_circle_i16.h       Q15 sin/cos/tan LUT + lerp
  fast_inv_trig_i16.h     inv sqrt / atan2 / phase_inc

signal_gen/
  siggen_play.*           Vulcan + HTTP front (PWM DDS + ADC capture)
  sig_insn.h / .cpp       ISA, builder, runner task
  sig_protocol.h / .cpp   SPI + I2C sample sinks
  siggen_wavetable.*      buffer fillers
  siggen_ledc.*           PWM backend
  siggen_i2s_pdm.*        PDM backend
  siggen_adc_cal.*        curve-fitting mV
  siggen_ctrl.*           older singleton API (still valid)
  siggen_types.h
```
