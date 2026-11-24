# OIRIA OS (ESP-IDF)

Smartwatch firmware. Dual purpose: a normal smart device, and an industrial or semi-universal controller for whatever hardware it meets — consumer gear, raw modules, or robotics.

## what

OIRIA OS is intended to behave like Linux for small devices that have no MMU, without containing any Linux components. It should be friendly enough for a layman and deep enough for an expert, with a clean path between the two. It is also meant to meet high performance criteria for microcontrollers.

The smartlink companion is part of that. The watch is both a wearable and a controller.

## why

Wear OS is not good enough. Many standard libraries (microGL and the like) do not meet the performance bar. Mixed drivers and manufacturers rarely work together because of side effects, nonstandard management, or nonstandard I/O. A small, owned stack avoids that.

## how

OIRIA OS is broken into state-machine layers with component managers, similar to how modern Linux handles services. Encapsulation is kept thin for performance. There is no user/kernel boundary and no MMU sandbox. Isolation is by convention and by declared capabilities. FreeRTOS sits under everything. Two long-lived managers (WindowManager and appManager) own the frame and the app list. Apps own their windows. Input is an event. Data moves as a DataItem when it needs to leave an app.

---

# Main services

**graphics system**

- name: WindowManager (`os_code/core/window_env/MWenv`)
- purpose: display compositor. Owns the window list, z-order, and the frame. Draws windows to the framebuffer and pushes dirty rows to the panel.
- architectural principles: apps build a `Window` and hand it to the manager. The manager borrows; it does not own the content. Dirty flags over full redraws. Double-buffered framebuffer, 45 fps cap. Style and widgets live in `window_env/ui`. DOM pack exists for the second chip; apply-to-LCD is not wired yet.

**desktop environment**

- name: apps + appManager focus
- purpose: the set of things the user sees and switches between (menu, watch face, health, browser, and so on). Focus is a pointer plus a highlight flag plus a layer number.
- architectural principles: an app is not a system object that requests a window. It constructs one. `set_focused_app` raises the window and sets the highlight. The input path delivers events only to the focused app. No desktop shell in the Unix sense — the menu and the focused app are the desktop.

**input system**

- name: input handler (`os_code/middle_layer/input`)
- purpose: turn hardware (encoder knobs, buttons, and later USB HID) into `InputEvent`s and route them.
- architectural principles: one packed event, one queue (`ProcInputQueTarget`). The input task produces; apps consume via `receive_event_input`. No device-node tree. Target enum decides where it goes. Keyboard, mouse, touch, and phone-as-input are not in the path yet.

**application scheduler and system**

- name: rshell (`os_code/core/rShell`)
- purpose: app lifecycle and scheduling on top of FreeRTOS. Registers apps, opens them, starts their task, routes input and pipes, and owns focus.
- architectural principles: `AppBase` is the unit. `start_task` is an `xTaskCreate` of a loop that calls `tick_app` at `tick_rate_hz` and feeds the watchdog. Input arrives on a separate path (`route_input_to_focused`). Pipes and DataItems are the long-term data currency; most apps still talk to their window directly. Capabilities are declared on the manifest (`USES_SD_CARD`, `STREAM_OUT_CAPABLE`, and others) and checked at the call for GPIO, wireless, and wake. The ELF loader is not a sandbox.

**scripting**

- name: Toplethos (`os_code/core/rs_vm`)
- purpose: on-device scripting layer for behavior that does not belong in C++.
- architectural principles: bytecode interpreter (`rsvm_compile` + `rsvm_run`). No JIT on the ESP32-S3. The VM asks the host through function pointers; the host is not a kernel. Desktop AOT and the full language tree live elsewhere.

---

**Build:** this repo **is** the ESP-IDF project. See [BUILD.md](BUILD.md).

```bash
. ~/esp/esp-idf/export.sh
git clone https://github.com/sera5m/OIRIA_OS_espIDF
cd OIRIA_OS_espIDF
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Do **not** dump this tree into `tusb_hid/main/`. That old wrapper is gone.

**WARNING**

This device uses a custom programming language named **Toplethos** as a scripting layer, for more detailed behavior and edge programming than bash or Lua. It is a C++/Rust-like language with several features inspired by Lisp. It can be interpreted (full support for a RISC-like 32-bit subset) and compiled on device (support incomplete — file bug reports, as unexpected behavior will occur; Xtensa LX7 is not fully documented, with mixed registers, bit width, and register rotation that do not behave like standard ARM or RISC-V).

Toplethos on the watch is `os_code/core/rs_vm/` (compiled in). The desktop/reference tree is separate and **not** required to flash:

**https://github.com/sera5m/toplethos-lang**

```bash
git submodule update --init --recursive   # optional, fills third_party/toplethos-lang
```

See `readme.md` for the longer manual page, plus `TOPLETHOS.md`, `SIGGEN.md`, `BUILD.md`.
