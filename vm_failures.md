# Watch VM failures (audit 2026-09-09)

Host I/O was unwired as of 2026-09-06. **Native host is now installed** (`rs_vm_host_esp.cpp` + collective UART listen). Remaining gaps:

| Item | Status |
|------|--------|
| file / ls / cd / pwd / cat / mkdir / rm | wired → `/sdcard` VFS (`d_sdc` mount) |
| `sh` subset | wired (`help`, `role`, `env`, `ls`…) |
| `exec(".vul")` | nested `rsvm_eval_file` |
| `open_app` | `appManager::close_current_and_open` |
| `mw_text` | toolbar text (no per-window id yet) |
| UART blob + stream | `rs_dom_link` RX task; types 0x10–0x17, ASCII `start sequence:` / `end sequence.` |
| DataPool | file-backed `/sdcard/rpool/<name>.rpool` (not live kernel heap) |
| env / sysconf | `ROLE`, `v_env` fields via `env_vars.h` |
| `native()` C | still `-1` (no TF / libc on device) |
| ADC | still 0 |
| `@parallel` | still sequential (`thread_cap=1`) |
| `rs_vm_latex.c` | still not in CMake SRCS |

## Pins

Collective UART1: **TX GPIO7, RX GPIO18**, 921600. GPIO8 is I2C SCL — do not use.

## Listen (slave / puppet)

Same firmware. `boot_role` PUPPET/SOLO/TYRANT all start `rs_dom_link_start_listen()`.

- **Blob:** RSDOM `VM_SRC` (0x11) or ASCII `run /sdcard/foo.vul`
- **Stream:** `start sequence:` … lines … `end sequence.`  or `<<VUL` … `VUL>>`
- Binary: `STREAM_BEGIN` 0x15 / `CHUNK` 0x16 / `END` 0x17
