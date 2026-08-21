# Fused `rshell_appmanager` (original zip + vulcan + serial patches)

Drop these over:

```
os_code/core/rShell/rshell_appmanager.hpp
os_code/core/rShell/rshell_appmanager.cpp
```

## Merged from

| Source | What |
|--------|------|
| `os_code.zip` rShell appmanager | base lifecycle, pipes, registry |
| `appmanager_list_apps_patch.*` | `RegisteredAppInfo`, `list_registered_apps`, `is_app_registered` (already in zip) |
| `appmanager_close_open_fix.patch` | `close_current_and_open(std::string)` by value + null-safe kill |
| `appmanager_vulcan_mode.patch.*` | `run_vulcan_script`, `send_vulcan_uart`, `vulcan_mode_enabled` |
| `appmanager_serial_terminal.patch.*` | `isConnectedToSerialMonitor`, terminal task, single script buffer |

## Build flag

- **Without** `RSVM_IN_FIRMWARE`: vulcan/serial APIs link as stubs / idle terminal (safe default).
- **With** `-DRSVM_IN_FIRMWARE`: real `rsvm_eval_file`, `rsdom_pack`, UART line reader.

Add rs_vm sources to the component CMake when enabling the flag.

## Serial teletype

```
start_serial_terminal();   // after boot if monitor attached
// over idf.py monitor:
//   run /sdcard/script.vul
//   print(1);
//   .
```
