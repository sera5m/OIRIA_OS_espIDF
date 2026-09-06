# Watch VM failures (audit 2026-09-06)

Check only. Host/OS wiring is **not** patched here.

## What runs on device

- `rsvm_compile` + `rsvm_run` (`RSVM_IN_FIRMWARE=1` in CMakeLists).
- Print, GPIO, delay_ms.
- Array opcodes: `arr_new` / `arr_newd` (4 dims), `a[i]`, `a[i,j]`.
- `include` / `import` in the parser.
- Shell *opcodes* 0xE0–F0 exist in `rs_vm.cpp`.

## Native features that do not work (host NULL)

`rs_vm_host_esp.cpp` only fills print + GPIO + delay + stub ADC.

| Feature | Hook / opcode | Result |
|---------|----------------|--------|
| File read/write | `file_read` / `file_write` | no-op |
| ls / cd / pwd | `sys_ls` / `sys_cd` / `sys_pwd` | nil |
| sh / sys | `sys_cmd` | nil |
| exec(.vul) | `sys_exec` | unwired |
| open_app | `sys_open_app` | unwired |
| Screen / mw_text | `mw_set_text` (0xE6) | no draw |
| UART VM | `uart_send` | NULL; RSDOM types exist |
| DataPool | `pool_op` | unwired |
| env / sysconf | `sys_env` / sysconf_* | unwired |
| native() | `native_call` | unwired |
| ADC | `adc_read` | 0 |

`vm_mdl_shell.cpp` is a stub. `VULCAN_SHELL.md` is aspirational.

## Screen DOM

No `RSVM_OP_DOM`. `RSDOM_TYPE_DOM` is a UART frame, not a script opcode. `mw_text` needs `host.mw_set_text`.

## Language (patched this pass)

- `print(sin(90))` C-style primaries in `rs_vm_parse.cpp`
- C-style `i32 a[2][3];` → ARR_NEW / ARR_NEWD
- `@memory_hard` property bit (1<<15) — persist is host-side; bit is recognized
- `rs_vm_latex.c` still **not** in CMake SRCS

## UART exec

`RSDOM_TYPE_VM` / `VM_SRC` → `rsvm_eval` on the collective link still unfinished. Serial `<<VUL` in appManager exists with RSVM_IN_FIRMWARE.

## Not firmware (by design)

ikitaku, gcc AOT, JIT.
