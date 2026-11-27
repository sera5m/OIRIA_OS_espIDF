# OIRIA OS — project plan

Written 2026-11-27 as if taking the project over. Inventory first, then the work.

This is not a release gate. It is the map of what exists, what is missing, and what has to be built before this is a managed project instead of a pile of experiments.

---

## Inventory

### Apps that exist and register themselves

| Name | Path | In menu | In CMake | State |
|---|---|---|---|---|
| WatchApp | applications/watch | yes | yes | face, alarms, ULP time push. Markup text. |
| MenuApp | applications/menu | (launcher) | yes | static menus. Several entries point at apps that do not exist. |
| HealthApp | applications/health | yes | yes | overview / graph / live. SetText. Sensors work. |
| FileViewerApp | applications/fileviewer | yes | yes | SD listing. Remounts if missing. |
| BrowserApp | applications/browser | yes | yes | HTML-to-markup. Offline help. Not a real browser. |
| Game2048App | applications/2048 | Games | yes | playable |
| PongApp | applications/pong | Games | yes | playable |
| SnakeApp | applications/snake | Games | yes | playable |
| SigGenApp | applications/siggen | Utilities | yes | signal gen, see SIGGEN.md |
| ScopeApp | applications/scope | Utilities | yes | scope view |
| ToplethosApp | applications/toplethosApp | Utilities | yes | VM console |

That is 11 registered apps. Three of them are games.

### Apps the menu lists that do not exist

- SettingsApp
- RemoteApp
- WirelessApp
- CalcApp
- StopwatchApp
- TimerApp

Selecting these logs "App not registered" and does nothing.

### Code that exists but is not an app

- toolkit/RemoteKeyboard (rubber ducky). Not in CMake. Not registered. hostusb/todo.txt still lists it.
- discord/discord_rpc.cpp. In CMake. Not an app. A side feature with no UI.
- siglab/trace_plot.hpp. Header only.

### Tooling that the README implies and that is not here

- **App uploader.** README points at https://github.com/sera5m/oiria-index for submitting apps. That repo is a catalog (pending/approved/rejected). There is no tool in this tree that packages an app, uploads it, or installs it on a watch.
- **ELF loader.** rshell_elif_link.cpp can load an ELF from a path. The menu entry is "Load test_app" — a hardcoded name, not a file picker. No build step produces an ELF the watch can load.
- **Desktop builder.** desktop/README.md says the GTK IDE is not part of this repo and lives at https://github.com/sera5m/toplethos-ide. desktop/toplethos_ide/ is a leftover overlay (toplethos_ide.py, toplethos_run.py, companion.py). It is not the builder. There is no command in this repo that takes app source and emits a loadable artifact.
- **Companion app.** https://github.com/sera5m/android_smartlink_app exists. USB works. BLE connect is written but the watch does not advertise because CONFIG_BT_ENABLED is unset in the committed sdkconfig.

### OS subsystems (what is actually in the tree)

- **Boot** (core/com/boot_mode_select.c, boot_role). Master-first, menu-held mode select, /bootseq serial rescue. Works for cold boot.
- **Power.** BMA400 shake-wake and ULP tags exist. app_main forces light_sleep_enable = false. No idle policy.
- **WindowManager** (window_env/MWenv). Compositor, dirty rows, 45 fps. Works.
- **UI framework** (window_env/ui). ui_widget tree, styles, fonts. No app uses it. Everything is SetText markup.
- **DOM** (mwenv_dom, rs_dom_link). Pack and parse. No apply-to-LCD. UART TX/RX start is stubbed in places.
- **Input** (middle_layer/input). Encoder and buttons. No keyboard, mouse, touch, or phone-as-input.
- **rshell** (core/rShell). App lifecycle, focus, pipes, DataItem, pump skeleton. Capabilities declared; some checks landed (GPIO, wireless, wake). ELF load exists but is not a product path.
- **Streams.** DataPool, InterProcessPipe, InterDevicePipe, PoolRing. CBOR sink logs and drops. Pump routing incomplete.
- **Notifications** (notification_sys). Local alerts. ULP alarm stubs.
- **VM** (rs_vm). Interpreter in firmware (RSVM_IN_FIRMWARE=1). No JIT. Desktop AOT is elsewhere.
- **Sensors.** BMA400 driver (shake-wake). MAX30102 + hr_metrics + hr_live + hr_profile. /sensor command replies "na" because devices are not global.
- **Display drivers.** ST7789v2 (primary), SSD1351 (added). fb_circle and fb_triangle are stubs.
- **Storage.** SD driver. File viewer. HR log flushes to /sdcard/hr.
- **Wireless.** WiFi autoconnect parser. BT code present, disabled in sdkconfig. LoRa and NFC are on the BOM only.
- **Multi-chip.** Collective UART initializes. InterDevice flush writes when ready. No hardware test. Tyrant does not open local apps.

---

## 1. Development infrastructure

This is the part that makes the project manageable. Without it, every app is a hand-edited C++ file in this repo.

### 1.1 Desktop builder

- Decide the artifact. ELF for native apps, or .bvul / source for Toplethos apps. They are different paths.
- One command that builds a native app against the watch SDK and emits an ELF the loader can read.
- One command that compiles a Toplethos script to bytecode the watch VM runs.
- The IDE (toplethos-ide repo) should call these, not reimplement them.
- Remove or clearly mark desktop/toplethos_ide/ as non-canonical so nobody edits the wrong tree.

### 1.2 Uploader and install

- A tool that takes the artifact, sends it to the watch (USB serial now, BLE later), and places it where the loader expects (SD path or a known directory).
- The watch side: a real "install" that writes the file, registers the app, and shows it in Misc without a rebuild.
- Replace the "Load test_app" menu entry with a file picker over the install directory.

### 1.3 Package catalog (oiria-index)

- Define the package format (manifest: name, version, capabilities, entry).
- The uploader pushes to the user's own index, or to a PR on oiria-index.
- The watch only runs approved=true, matching the existing catalog rule.
- None of this exists as code. The README already points at the repo.

### 1.4 Companion as a dev tool

- USB terminal already works.
- Add: push file, list installed apps, launch app, pull logs.
- BLE variants of the same once the watch advertises.

### 1.5 Repo hygiene

- One place for "what works." PROJECT_PLAN.md is the map. RELEASE_TODO.md is the cut line. stuff i want to do.txt is stale — fold anything still true into this file and stop editing the old one.
- sdkconfig committed state must match sdkconfig.defaults, or the defaults are a lie.
- A version string in the firmware and a tag when you cut.

---

## 2. Apps — what to build

You have 11 registered apps. Three are games. The menu promises six more that do not exist. A watch someone else can use needs the missing everyday apps before it needs more games.

### 2.1 Apps that must exist

- **Settings.** Brightness, sleep timeout, boot role, BT on/off, about/version. The menu already has the entry.
- **Stopwatch.** Start / stop / lap. Menu entry exists.
- **Timer.** Set, run, notify. Menu entry exists.
- **Calculator.** Four functions is enough. Menu entry exists.
- **Alarms.** The watch app has alarm structs and ULP bits. Surface them as their own screen or a clear mode inside WatchApp.

### 2.2 Apps that should exist for the controller claim

- **Remote.** Media keys over USB/BLE to the phone. The companion already sends media packets the other way. Menu entry exists.
- **Wireless.** WiFi status, BT status, scan. Menu entry exists. rs_autoconnect parses a conf; there is no UI.
- **File manager actions.** File viewer lists. Add open, delete, and "install this ELF."
- **Sensor viewer.** Live accel, temp, HR, SpO2. Health shows HR. Nothing shows the BMA400.

### 2.3 Apps that exist and need to be finished

- **WatchApp.** Survive sleep. Show time from ULP on wake. Alarms that fire. Widget tree instead of markup.
- **HealthApp.** One screen on the widget tree (title, BPM, SpO2, bar, sparkline). Passive log survives reboot. Label die temp as die temp.
- **FileViewerApp.** Empty state when SD is absent. Install action.
- **BrowserApp.** Decide: ship as a local markup viewer, or hide it. It is not a browser.
- **Rubber ducky.** Add to CMake, register it, or delete it. A half-finished HID injector should not sit in the tree.
- **Scope / SigGen.** One-line menu description. Known-good input. Or hide.

### 2.4 Apps to add later

- Notifications inbox.
- Music control that is not just media keys.
- Maps or navigation (needs a data source).
- Camera (no sensor).
- App store UI over the oiria-index catalog.

### 2.5 Apps to leave as extras

2048, pong, snake, discord RPC, toplethos console. They can stay. They are not the product.

---

## 3. OS subsystems — what to finish

### 3.1 Boot and power

- Idle policy: N seconds no input → light sleep. Wake sources: encoder, button, BMA shake, BLE, ULP alarm.
- Turn light_sleep_enable on for real.
- Confirm /bootseq works after a wake, not only cold boot.
- Measure and record current in active, BLE-idle, and sleep.

### 3.2 Display and UI

- Implement fb_circle and fb_triangle.
- Draw the title bar (WIN_OPT_SHOW_TOP_BAR_MENU is ignored).
- Port one app (Health or Menu) to the ui_widget tree so the framework is proven.
- Make the toolbar a Window subclass that is actually used.
- Clip to window bounds everywhere widgets draw.
- mwdom_apply so a DomFrame becomes draws. Required for the second chip, not for a single-watch release.

### 3.3 Input

- Document the event path (queue → route_input_to_focused → receive_event_input).
- Phone-as-input: companion sends knob/button events over the link.
- USB HID host for a keyboard. The middle layer has rubber-ducky and hostusb notes; the path is not product.
- Touch, if the hardware ever has it. Not in the current BOM.

### 3.4 rshell and apps runtime

- list_registered_apps is used by the menu. Confirm it is complete.
- ELF load: file picker, install directory, error surface when the ELF is bad.
- Capability checks at the remaining call sites, or document which are enforced.
- Pipe and DataItem: either use them in one app or stop advertising them as the data path.

### 3.5 Sensors

- Global or snapshot access so /sensor returns real numbers.
- Confirm MAX30102 and BMA400 on the shipping hardware with one recorded session.
- Motion gate is a threshold. Say so.
- Die temp is not skin temp. Say so.

### 3.6 Wireless

- Enable BT in sdkconfig. Confirm advertise, connect, sleep window, heartbeat.
- WiFi: a status screen, not just a conf parser.
- LoRa and NFC: BOM only. Drivers do not exist. Do not promise them.

### 3.7 Storage

- SD mount failure is handled in the file viewer. Make it a system state the menu can show.
- HR log path (/sdcard/hr) documented.
- CBOR sink: later. It logs and drops today.

### 3.8 Notifications

- Local notify works. ULP alarm is a stub.
- One path: alarm or timer fires → notification → screen or haptic.
- No haptic driver is obvious in the tree. Confirm hardware before promising vibration.

### 3.9 Multi-chip

- Not required for a single-watch release.
- Collective UART needs a hardware test before it is called supported.
- RSDOM exec and tyrant/puppet stay experimental until the puppet paints.

### 3.10 VM

- Interpreter is in the build.
- On-device compile is incomplete (Xtensa). Do not advertise it.
- Desktop IDE and language repo stay separate. This repo consumes bytecode.

---

## 4. Companion app

- USB: terminal, capture, media, sleep window. Works.
- BLE: connect and write are coded. Watch must advertise first.
- Health export: dropdown of writers. File CSV/JSON work. Health Connect is queued, SDK not linked.
- Missing for dev: file push, app install, log pull, sensor read that returns real values.
- Missing for product: background hold on the GATT link, settings sync.

---

## 5. Hardware

- BOM is at hardware/docs/components_bom.csv. PCB is in the Drive zip. Manufacturing is paused.
- Next physical step is a hand-built test unit, not a watch-shaped spin.
- Drivers present: ST7789, SSD1351, BMA400, MAX30102, encoders, SD.
- Drivers absent: LoRa1262, PN532, IST8310, DS3231 (RTC is ULP/software), haptic, touch.

---

## 6. Order of work

Do these in order. Each one unblocks the next.

1. **Dev infrastructure.** Desktop build command, uploader, install directory, file picker instead of "Load test_app." Until this exists, every app change is a firmware rebuild.
2. **Missing everyday apps.** Settings, stopwatch, timer, calculator. The menu already promises them.
3. **Power and BT.** Light sleep on. BT enabled. Phone holds the link.
4. **One app on the widget tree.** Health or Menu. Proves the UI framework.
5. **Watch face and health as the demo.** Time after sleep. One recorded HR session.
6. **Finish or delete** rubber ducky, and hide or describe scope/siggen/browser.
7. **Tag a release** with a version string and a "what works" list that matches the tree.

Everything else — multi-chip, CBOR, 1-Wire, LoRa, Health Connect, more games — is after that.
