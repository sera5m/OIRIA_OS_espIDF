# RS-VM → Watch OS (ESP32-S3) port notes

## What to copy into the firmware tree

```
os_code/core/rs_vm/
  vm/
    rs_vm_opcodes.h
    rs_vm.hpp / rs_vm.cpp
    rs_vm_parse.hpp / rs_vm_parse.cpp
    rs_vm_host_esp.cpp
  vm_modules/
    vm_mdl_immut/
    vm_mdl_thread/
    vm_mdl_property/
    vm_mdl_flowmap/
  debugger/          # optional on-device; useful for UART logs
```

App (console test):

```
os_code/applications/vulcan/
  MS_vulcanapp.hpp
  MS_vulcanapp.cpp
```

(or drop the two files next to other MS_* apps and fix includes)

## CMake / component SRCS

```cmake
# core VM
"os_code/core/rs_vm/vm/rs_vm.cpp"
"os_code/core/rs_vm/vm/rs_vm_parse.cpp"
"os_code/core/rs_vm/vm/rs_vm_host_esp.cpp"
"os_code/core/rs_vm/vm_modules/vm_mdl_immut/vm_mdl_immut.cpp"
"os_code/core/rs_vm/vm_modules/vm_mdl_thread/vm_mdl_thread.cpp"
"os_code/core/rs_vm/vm_modules/vm_mdl_property/vm_mdl_property.cpp"
"os_code/core/rs_vm/vm_modules/vm_mdl_flowmap/vm_mdl_flowmap.cpp"

# test app
"os_code/applications/vulcan/MS_vulcanapp.cpp"
```

Include dirs: add `os_code/core/rs_vm/vm` and `os_code/core/rs_vm` (for `vm_modules/...`).

## Menu entry

In `app_menu` utilities (or misc):

```cpp
{"Vulcan", "VulcanApp", false},
```

`MS_vulcanapp.cpp` registers `VulcanApp` via the same `AppRegistration` pattern as Pong.

## Host

- Default: `rsvm_install_esp_host(vm)` → ESP_LOG only
- UI app: `rsvm_install_esp_host_ui(vm, this)` → on-screen log buffer

`print_char` is required for `printc` / progressive strings.

## Memory / stack

- VM structs are large (code 4K, heap 2K, str pool 1K). Prefer **static** or PSRAM
  allocation if the app task stack is tight.
- Suggest task stack ≥ 12–16 KB for VulcanApp (parse + run + UI).
- `RSVM_MAX_STEPS` default 200k; embedded sample uses `set_step_depth 50000`.

## Optional next steps

1. Load `.vul` / `.bvul` from microSD (`/sdcard/apps/vulcan/`).
2. Wire `RSDOM_TYPE_VM` UART path for puppet/tyrant collaborative run.
3. Drop `debugger/` stats to UART on STEP_LIMIT.
4. Replace Window `draw_text` API names if your MWenv differs (stubs in app).

## Window API assumptions

`VulcanApp` calls:

- `WindowManager::instance().create_window(title, x, y, w, h)`
- `win->set_bg`, `win->clear`, `win->draw_text(x, y, str, color)`
- `appManager::instance().close_current_and_open("app_launcher_menu")`

If your tree uses different names, adjust only `MS_vulcanapp.cpp`.

## Shell / MWenv / appManager fusion

See `VULCAN_SHELL.md`. C++ apps stay firmware; Vulcan is bash-like:

```
sh / ls / cd / pwd / exec / open_app / env / pool / mw_text / uart_send
```

Host bridges: SD, DataPool, Window::SetText, appManager::close_current_and_open, rsdom UART.

Secondary mode patches: `artifacts/appmanager_vulcan_mode.patch.*.txt`
