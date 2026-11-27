# OIRIA OS — from working test to semi-finished release

Written 2026-11-27 after a pass over the tree. This is the work that turns
the current test build into something you can hand to someone else and
call it a release. It is not a feature wishlist.

Priority:
- **must** — blocks a release
- **should** — needed for the dual wearable/controller claim
- **later** — real, but not for this cut

---

## 1. Boot and power

**must**
- Turn on light sleep for real. `app_main` still forces `light_sleep_enable = false`. The BMA400 shake-wake and ULP path exist; the main CPU never enters the sleep they were built for.
- Confirm the early rescue window and `/bootseq` still work after a sleep cycle. Cold boot is tested. Wake-from-sleep is not.
- Decide the idle policy: how long with no input before light sleep, and what wakes it (encoder, button, BMA shake, BLE notify, ULP alarm). Write it down and implement that one policy.

**should**
- Measure current draw in the three states (active UI, BLE connected idle, light sleep) and put the numbers in the README. A release without a power number is a demo.

**later**
- Deep sleep with ULP keeping time and alarms. The watch app already pushes time to the ULP; the return path is not a product.

---

## 2. Bluetooth and the phone link

**must**
- Enable BT in the real `sdkconfig`. `sdkconfig.defaults` asks for NimBLE; the committed `sdkconfig` still has `CONFIG_BT_ENABLED` unset, so `ble_link_start()` logs and returns. Rebuild with it on and confirm the watch advertises as "Oiria".
- Finish the GATT path on the phone. The app connects, discovers, and writes. Confirm a sleep-window packet (`53 4C <sec>`) actually changes the connection interval and that heartbeats arrive while the watch is otherwise idle.
- Keep the link alive in the background. The phone needs a foreground service (or CompanionDeviceService) so Android does not kill the GATT connection when the app is backgrounded.

**should**
- Wire `/sensor accel|temp|hr|spo2` to the real drivers. `remote_cmds.c` replies `na` because the BMA400 and MAX30102 devices are not reachable from the command handler. Expose the live device (or a snapshot) so the phone can read them.
- Make the phone the place you set boot role and config vars over BLE, not only USB. The text commands already exist; they need to work on the notify/write chars after boot, not only in the early serial window.

**later**
- BLE as a HID (keyboard/media) so the watch can drive a PC without the companion app.
- Health Connect writer in the Android app. The dropdown and the queued file exist; the SDK is not linked.

---

## 3. Display and UI

**must**
- One real screen on the widget tree. Health and the menu still call `SetText` with markup. Until one of them builds a `ui_widget_t` root (`ui_w_title`, `ui_w_list`, `ui_w_bar`) and draws through `Window::root`, the framework is untested and the old path stays the default.
- Implement `fb_circle` and `fb_triangle`. They are stubs that log. Rounded corners and the pie/arc widgets depend on them or on the span fallback. A release UI should not log on every draw.
- Draw the title bar. `WIN_OPT_SHOW_TOP_BAR_MENU` is checked and then ignored. Every window needs a name.

**should**
- Make the toolbar a `Window` subclass for real. The class exists; the old free functions and fixed 24/32 px offsets are still what runs.
- Pick a font set and stop hardcoding the AVR face at draw sites. `ui_font()` is the fallback; a few call sites still name the face directly.
- Clip is parent-bounds only. A dirty-region clip (or at least a window clip that the widgets respect) stops the overdraw that makes the UI feel unfinished.

**later**
- `mwdom_apply`. Pack and parse exist. Nothing turns a `DomFrame` into draws. The second chip cannot paint until this lands.
- More fonts, patterns, and the cyberpunk face. Polish, not a gate.

---

## 4. Apps

**must**
- Menu that launches the apps you intend to ship, and only those. Dead or half-broken entries should be hidden.
- Watch face that shows time after sleep and after a time push to the ULP. This is the app a layman opens.
- Health overview that uses the widget tree and shows the last BPM / SpO2 / band without requiring the live mode.

**should**
- File viewer that does not fall over when the SD card is absent. It already remounts; it should show a clear empty state.
- Finish or remove the rubber ducky app. `hostusb/todo.txt` still lists it. A half-finished HID injector is a support burden.
- Scope and siggen: decide if they ship in this cut. If yes, they need a one-line description in the menu and a known-good input path. If no, hide them.

**later**
- Browser, Discord RPC, 2048, pong, snake. They can stay as extras. They are not the release.
- 1-Wire. No driver in the tree. Add it when a real sensor needs it.

---

## 5. Sensors and data

**must**
- Confirm the MAX30102 and BMA400 paths on the hardware you will ship. The drivers exist and the HealthApp shows numbers; a release needs one recorded session that matches a known source (finger on the sensor, shake wake that actually wakes).
- Passive HR log must survive a reboot. NVS block and the SD flush exist. Test the 80% flush and the reload.

**should**
- Motion gate is a threshold, not fusion. Document that. Do not claim fusion in the release notes.
- Die temp is not skin temp. Label it, or do not show it.

**later**
- Full sensor fusion, SpecMAR, always-on HR via ULP. The ULP cannot drive the MAX30102 today.
- CBOR storage sink. `architectureGoal.md` assumes CBOR tags on DataItem; the sink logs and drops.

---

## 6. Multi-chip and controller mode

**must**
- Nothing in this section is required for a single-watch release. Do not block on it.

**should**
- Collective UART is initialized. Confirm a second ESP32-S3 actually receives a frame. `InterDevicePipe::flush` writes when the link is ready; the ready path needs a hardware test.
- RSDOM exec (CMD/RESULT) is designed. The puppet worker and the host UART send are not a product. Leave it documented as experimental.

**later**
- Tyrant/puppet as a supported mode with a setup step in the companion app.
- External hardware (LoRa, NFC, raw modules) as a controller. The BOM lists the parts; the firmware does not speak them yet.

---

## 7. Build, docs, and release hygiene

**must**
- One command that builds, flashes, and opens the monitor, and it works on a clean ESP-IDF export. `BUILD.md` exists. Follow it on a clean machine once and fix what breaks.
- `sdkconfig` committed state matches what you ship. Right now BT is off in `sdkconfig` and on in `sdkconfig.defaults`. Pick one.
- Remove or gate the debug prints that fire on every frame or every command. A release log should be quiet unless something failed.
- Version the release. A tag, a version string in the firmware, and a one-paragraph changelog.

**should**
- README already has the what/why/how. Add the power number, the known limits (no fusion, die temp, BT requires reconfigure), and a "what works today" list that matches the tree.
- The lowercase `readme.md` man page is useful. Link it and keep it in sync, or fold the still-true parts into `README.md` and archive the rest.

**later**
- CI that builds on push. Not required for a first release you flash yourself.
- Packaging for the companion app (signed APK, version). The app is a tool; it can stay sideload for this cut.

---

## Suggested order

1. Power policy and a measured sleep current. (Section 1)
2. Enable BT, confirm advertise, confirm the phone holds the link and the sleep window works. (Section 2)
3. One app on the widget tree, title bar, and `fb_circle`. (Section 3)
4. Menu + watch face + health overview as the three apps you will demo. (Section 4)
5. One recorded HR session and a shake wake on the hardware. (Section 5)
6. Clean `sdkconfig`, quiet logs, tag. (Section 7)

Multi-chip, CBOR, 1-Wire, extra apps, and Health Connect are after that.
