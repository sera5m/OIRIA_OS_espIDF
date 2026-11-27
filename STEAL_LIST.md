# What to steal from Wear OS and Flipper Zero

Reference for OIRIA OS. Written 2026-11-27.
This is the feature inventory of the two products this project sits between,
mapped onto what this tree can actually build.

Wear OS is a second phone on a wrist. Flipper Zero is a pocket multi-tool.
OIRIA is supposed to be both, on an ESP32-S3 with no MMU, and not suck at either.

---

## Wear OS — the surfaces, not just the apps

Wear OS is not a list of apps. It is five surfaces. Steal the surfaces.

### Watch face

The first thing on screen. Time, plus complication slots.
A complication is one glance unit: HR, battery, next alarm, steps, weather.
The user picks which app fills which slot. The face does not own the data.

Steal: a face with 2–4 slots. Each slot is a small widget that reads from a
named source (hr.bpm, alarm.next, battery.pct). Apps publish; the face displays.

You have: WatchApp with time and alarm structs. No slots. Markup text.

### Tiles

Swipe off the face. One screen, one job, no scrolling. Weather, timer,
start-workout, media. Tap opens the full app.

Steal: two or three tiles. HR glance. Next alarm. Media controls.
Implement as windows the face can hand off to, not as a new framework.

You have: nothing. The menu is the only launcher.

### Notifications

Time-sensitive, glanceable, one or two actions (dismiss, open).
Not a feed. A queue.

Steal: a queue with a source, a title, a body, and an optional action id.
Alarm and timer are the first two sources. The phone link is the third.

You have: notification_sys with local alerts. ULP alarm is a stub.
No haptic driver confirmed.

### App launcher

Grid or list. Opens an app. That is the whole job.

Steal: the menu you have, but only list apps that exist.
Hide the six entries that 404.

You have: MenuApp. It lists Settings, Remote, Wireless, Calculator,
Stopwatch, Timer. None of those apps exist.

### Settings

Brightness, timeout, connectivity, about/version, storage.

Steal: one screen. Brightness, sleep timeout, boot role, BT on/off, version.

You have: the menu entry. No app.

---

## Wear OS — the stock apps worth taking

A Pixel Watch ships roughly: Clock, Contacts, Wallet, Maps, Calendar,
Gmail, Assistant, Home, YouTube Music, Fitbit/Health.

### Take these

**Clock.** Alarms, timer, stopwatch, world clock.
This is why it is a watch. Split it:
- Watch face shows time and next alarm.
- Timer is its own app (set, run, notify).
- Stopwatch is its own app (start, stop, lap).
- Alarms are a list inside WatchApp or their own screen.
You have the alarm structs and the ULP bits. You do not have timer or stopwatch.

**Health glance.** HR, SpO2, steps if you ever have them.
Not a medical device. Label it.
You have HealthApp (overview / graph / live), MAX30102, motion gate, RLE log.
Die temp is die temp. Motion gate is a threshold, not fusion.

**Settings.** See above.

**Media remote.** Play/pause, next, prev, volume. Sends to the phone.
The companion already sends media packets the other way.
Build the watch side: four buttons, one packet format, over USB now and BLE later.

**Notifications.** See above.

**Calculator.** Four functions. The menu already promises it.

**File manager.** List, open, delete, and "install this."
You have a viewer. Add the actions.

**Contacts / phone.** Only if the companion holds a link and can place a call
or show an incoming one. Otherwise skip. You are not a phone.

### Take later, or never

**Weather.** Needs a source (phone bridge or a fetch). Later.

**Maps / turn-by-turn.** Needs GPS. Not in the BOM. Skip.

**Wallet / pay.** Needs a secure element. PN532 on the BOM is a reader,
not a payment chip. Skip.

**Music offline.** Needs BT audio and a storage story. Later.

**Messaging.** Needs the phone bridge and a keyboard. Later.

**Smart home.** Needs a protocol you do not speak. Skip.

**Assistant / Gemini.** Not your problem. Skip.

**Calendar / Gmail.** Phone bridge features. Later, and only as a viewer.

---

## Flipper Zero — the actual menu

These are tools. The community catalog is extra.

### Sub-GHz

Read, save, replay signals under 1 GHz. CC1101. 315/433/868/915.
Garage doors, sensors, remote keyless, weather stations.
Range claims around 50 m. Rolling codes do not replay. That is physics, not a bug.

You have: LoRa1262-915 on the BOM. No driver. LoRa is not CC1101 OOK.
You can do LoRa packets later. You cannot do Flipper-style replay without
a different radio. Do not promise Sub-GHz replay.

### 125 kHz RFID

Read, save, emulate low-frequency badges (EM4100, HID Prox, Indala, etc.).

You have: nothing. No coil, no chip. Skip.

### NFC (13.56 MHz)

Read, save, write (rewritable only), emulate. MIFARE Classic/Ultralight,
DESFire, FeliCa, HID iClass, NFC Forum.

You have: PN532 on the BOM. No driver.
Steal the smallest slice: read UID, show it, save it to SD.
Write and emulate are later, and only on cards you own.

### Infrared

Learn and send. Built-in library for TVs and ACs. NEC, RC5/RC6, Samsung, etc.

You have: no IR LED, no receiver. Skip unless you add the parts.

### GPIO

Pin on/off, and USB-to-UART/SPI/I2C bridge. Also used as a flasher and debug probe.

You have: the serial rescue path and the companion USB link.
Steal: a "USB UART bridge" mode that is just the existing console,
plus a pin toggler for the pins you already break out.
Do not build a general GPIO studio until the pins are documented.

### iButton (1-Wire)

Read, save, emulate 1-Wire keys.

You have: no 1-Wire driver, no contact. Skip until a real sensor needs it.

### Bad USB

Rubber Ducky scripts over USB HID. Upload a script, plug in, it types.

You have: toolkit/RemoteKeyboard written, not in CMake, not registered.
Steal: finish it or delete it. A half-finished HID injector is a liability.
If you ship it, it is a tool for your own machines, documented as such.

### U2F

Security key. WebAuthn.

You have: nothing. Skip. ESP32-S3 can do this later; it is not the product.

### Archive

Saved signals, tags, remotes, payloads, sorted by type.

Steal the idea: one directory on SD per tool (nfc/, scripts/, hr/).
The file viewer already lists. Add a filter by folder.

### Apps catalog

Community apps, installed from a catalog, stored on SD.

Steal: this is oiria-index. Catalog exists. Uploader and install path do not.
Build the install path before you build more apps.

### Settings

Brightness is N/A on Flipper. You need timeout, BT, about, storage.

---

## The overlap that is your product

A watch face with glance slots.
Clock tools (timer, stopwatch, alarms as a screen).
Health glance and a log.
Settings.
A media remote to the phone.
A small hardware-tool set: NFC UID, Bad USB (or not), serial bridge.
An install path so the sixth app is not a firmware rebuild.

Not a second phone. Not a Flipper clone. The parts of each that fit
an ESP32-S3 with a display, a heart sensor, an accelerometer, and a phone link.

---

## Mapped to the current tree

| Steal | From | In tree now | Next concrete step |
|---|---|---|---|
| Face with slots | Wear | WatchApp, no slots | Define 2 slot sources (hr.bpm, alarm.next) and draw them |
| Tiles | Wear | none | One HR tile window the face can open |
| Timer | Wear | menu entry only | New app: set, run, notify |
| Stopwatch | Wear | menu entry only | New app: start, stop, lap |
| Alarms screen | Wear | structs in WatchApp | List + enable in WatchApp |
| Settings | Wear | menu entry only | Brightness, timeout, BT, version |
| Media remote | Wear | companion sends to watch | Watch sends to phone |
| Calculator | Wear | menu entry only | Four functions |
| File actions | Wear | viewer | Add delete and install |
| Notifications | Wear | stub | Queue + alarm as first source |
| NFC UID | Flipper | PN532 on BOM | Driver: read UID, show, save |
| Bad USB | Flipper | code, not in build | Add to CMake and register, or delete |
| Serial bridge | Flipper | rescue console | Document it as the bridge |
| Archive folders | Flipper | SD + viewer | Convention: /sdcard/<tool>/ |
| App catalog | Flipper | oiria-index repo | Install path on the watch |
| Sub-GHz replay | Flipper | LoRa on BOM, no driver | Do not promise |
| 125 kHz / IR / iButton / U2F | Flipper | no hardware | Skip |
| Maps / Wallet / offline music | Wear | no hardware | Skip |

---

## Order, if you build from this list

1. Settings, timer, stopwatch. The menu already promises them. They are small.
2. Face slots for HR and next alarm. Makes the face a face.
3. Media remote, watch to phone.
4. Finish or delete Bad USB.
5. NFC UID, once you care about the PN532.
6. Notification queue, fed by timer and alarm.
7. Install path, so the catalog is real.

Everything else on this list is a want, not a gate.
