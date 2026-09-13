# Function generator + scope (Vulcan)

Standout watch feature: **reverse oscilloscope** (AWG) and **normal oscilloscope**.

| Mode | What | Hardware |
|------|------|----------|
| Reverse | You define a wave → it comes out a GPIO | LEDC PWM. Sine/tri/saw = duty DDS + **RC LPF** (1kΩ + 100nF). Square is hardware PWM. |
| Normal | GPIO in → millivolts / web plot | ADC1 oneshot (ADC2 fights Wi‑Fi) |

ESP32-S3 has **no DAC**. Corz.org used GPIO 25/26 DACs on classic ESP32. We keep the *commands*, not the analog block.

## Web console

On boot (after Wi‑Fi): `http://<watch-ip>/`

- STA from `autoconnect.conf`, else AP **OIRIA-vulcan** / **vulcanvulcan**
- **Script** — drop or paste a `.vul`, save to `/sdcard/inbox` or RAM, run
- **Wave** — sine/square/triangle/saw + Corz one-liners
- **Scope** — capture ADC, draw in the browser (Bojan-style)

Works on **head (solo/tyrant)** and **secondary (puppet)** — same firmware. Tyrant `native("wave",…)` also UART-sends the same call to the puppet so the worker can drive the pin.

## Vulcan

```
native("wave", kind, hz, duty, pin, amp)   // 0 sine 1 square 2 tri 3 saw
native("wave_stop")
native("wave_freq", hz)
native("wave_duty", pct)
native("sweep", f0, f1, ms)
native("adc", pin)                         // millivolts
```

Examples: `os_code/core/rs_vm/examples/wave.vul`

## Corz → Vulcan

| Corz | Vulcan |
|------|--------|
| `s` / `r` / `t` | `native("wave", 0\|1\|2, …)` |
| `2000` `2k` | `native("wave_freq", 2000)` |
| `p25` | `native("wave_duty", 25)` |
| `a1..4` | amp 12/25/50/100 |
| `stop` `.` | `native("wave_stop")` |
| loops/macros | a `.vul` file |
| musical `*a` | not ported (use Hz) |

Web `POST /cmd` still accepts the one-letter Corz line for muscle memory.

## Pins

- Default **out** GPIO **4** (LEDC)
- Default **scope** GPIO **1** (ADC1). Must be an ADC1 pad.

## Missed AWG extras (later)

Burst, gated sync, DC offset (needs extra analog), I2S PDM path (`siggen_i2s_pdm` already compiled). Sweep **is** implemented.
