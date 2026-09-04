# Vm_accel — compile-target trapdoor cores

Per-target acceleration for Vulcan. Firmware CMake does **not** list this
folder unless you opt a watch source in. Desktop builds include it.

| Target | Macro | Trapdoor / accel |
|--------|--------|------------------|
| Watch (ESP32-S3) | `RSVM_TARGET_WATCH` | Q15 sin/cos LUT + cache-friendly walk, small stacks |
| Desktop | `RSVM_TARGET_DESKTOP` | larger stacks, optional JIT hook (`rsvm_accel_jit_*`) |

Language side: `@target watch` / `@target desktop` or
`set_compile_target watch|desktop;` (parser can map these later).

Watch does **not** JIT. Hardware accel here means table + sequential
access so the S3 I-cache / D-cache stay hot during signal loops.
