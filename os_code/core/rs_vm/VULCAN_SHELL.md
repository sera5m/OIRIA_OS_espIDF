# Vulcan as the system shell (bash analogue)

## Intent

Firmware **C++ apps stay baked in** (Pong, Snake, File Viewer, …).  
Vulcan (`.vul` / `.bvul`) is the **scripting / orchestration layer** — like bash on Linux:

| Linux | OIRIA |
|-------|--------|
| bash | Vulcan RS-VM |
| ELF binaries | C++ `AppBase` apps in firmware |
| `systemctl` / open app | `open_app("Name")` → `appManager::close_current_and_open` |
| `ls` / `cd` / files | SD + DataPool via host |
| pipes | rShell pipes (future: Vulcan bridge) |
| ssh remote | UART `RSDOM_TYPE_VM*` packets |

## What was fused

### Opcodes `0xE0–0xE9` (host-backed)

| Op | Builtin | Role |
|----|---------|------|
| SYS_CMD | `sh("…")` / `sys("…")` | generic command string |
| SYS_EXEC | `exec("path.vul")` | run script from storage |
| SYS_OPEN | `open_app("Pong")` | launch registered C++ app |
| SYS_LS | `ls(".")` | directory listing |
| SYS_CD / PWD | `cd` / `pwd` | working directory |
| MW_TEXT | `mw_text(id, "…")` | MWenv window text |
| UART_SEND | `uart_send(type, data)` | collaborative link |
| POOL_OP | `pool(name, op, data)` | DataPool / rpool |
| SYS_ENV | `env("ROLE")` | boot role / env |

### Host callbacks (`rsvm_host_t`)

Wire on ESP to:

- `sys_open_app` → `appManager::close_current_and_open`
- `file_read` / `sys_ls` / `sys_cd` → SD + VFS
- `pool_op` → DataPool / `save_to_rpool` / `load_from_rpool`
- `mw_set_text` → `Window::SetText` / DOM pack on puppet path
- `uart_send` → `rsdom_pack` + UART write
- `sys_exec` → nested `rsvm_eval_file` (or queue on manager)

Desktop stubs live in `rs_vm_desktop_test.cpp` for bring-up.

### App framework secondary mode

Patches (do not replace AppBase lifecycle):

- `appmanager_vulcan_mode.patch.hpp.txt`
- `appmanager_vulcan_mode.patch.cpp.txt`

Adds optional:

```cpp
appManager::run_vulcan_script(path);
appManager::send_vulcan_uart(type, payload, len);
appManager::vulcan_mode_enabled();
```

C++ apps remain the primary runtime. Vulcan is invoked **as needed** (menu, UART, automation).

### UART

`RSDOM_TYPE_VM` / `VM_SRC` / `VM_OUT` / `VM_ERR` / `VM_ACK` in `rs_dom_link.hpp`.

## Example

```vulcan
print(sh("help"));
print(pwd());
print(ls("/sdcard"));
open_app("Snake");
mw_text(0, "scripted UI");
uart_send(0x11, "print(42);");
```

## Extra bash surface

| Builtin | Notes |
|---------|--------|
| `grep(text, pat)` | line substring filter |
| `cat(path)` | file → string |
| `echo(s)` | print + return |
| `head(text, n)` | first n lines |
| `wc(text)` | line count |
| `sh("mkdir …")` / `rm` / `nano` | host command string |
| `sysconf("brightness")` | read `v_env` field |
| `sysconf_set("brightness", 128)` | write `v_env` field |
| `@unspecifiedout` | fn prop — dynamic out type (smart-ptr style) |

## Serial teletype (appManager)

- `isConnectedToSerialMonitor`
- FreeRTOS task `"terminal"` (~8 KB stack)
- **One** script buffer (`.vul` / `.cvul`) — `run path` or multi-line then `.`
- Patches: `appmanager_serial_terminal.patch.*.txt`

## sysconf ↔ EnvConfig

Keys (subset): `brightness`, `fpsTarget`, `headless`, `debugMode`, `safeMode`,
`screen_dim_w/h`, `batteryPercent`, `charging`, `cpuMhzTarget`, `bootCount`, `ROLE`.

ESP helpers: `vm/rs_vm_sysconf_esp.hpp.txt`
