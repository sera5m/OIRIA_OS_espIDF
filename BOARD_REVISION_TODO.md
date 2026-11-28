# Board revision TODO

Reviewed from `device_info/ESP32_Smartwatch_final/esp32smart/esp32smart.kicad_sch` (and the same folder's `bom.csv` / `esp32smart.csv` where noted), plus firmware `hardware/wiring/wiring.h`. Items below are things a respin should fix so the firmware can actually do the corresponding job. Single-point touch is fine by design and is not a todo.

`wiring.h` and the older BOM CSVs do not match this schematic (different refdes, and many GPIOs). Treat the schematic as the board truth; update firmware after the respin.

Unused expander pins for new signals: TCA9539 `IO1_3`–`IO1_7`. Unused ESP32 pins: IO35, IO36, IO37, IO43, IO44 — none of these are ADC.

---

## Must

- [ ] **Battery sense divider.** `+BATT` (J9 pin 1, TP4056 `BAT`) never reaches an ESP32 ADC pin. The only 100k on the board (R29) is the U13 CE pulldown to GND, not a divider. Add 2×100k from `+BATT` to GND, midpoint to an ADC1 pin, 100nF from the midpoint to GND. Optionally gate the divider with a spare load switch or MOSFET so it is not a constant ~21µA drain.

  There is **no free ADC1 pin today**: GPIO1–10 are all used, and GPIO1 is `LCD_BL` (do not reuse). Free one first. Best candidates are GPIO5 (`I2S_1`) or GPIO6 (`I2S_2`), which only go to pogo header J8. ADC2 (GPIO11–20) fights Wi-Fi; do not use it for battery.

- [ ] **TP4056 `~CHRG` / `~STDBY` are unconnected.** U12 pins 7 and 6 have no-connects. They do not go to an ESP32 GPIO, the TCA9539, or LEDs. Firmware cannot tell charging from full or from unplugged. Route `~CHRG` (and optionally `~STDBY`) to spare expander GPIOs and/or indicator LEDs.

- [ ] **USB-C data works in only one orientation.** J7 `D+`/`D-` are wired on the A-side only (`A6` → `USB_D+`, `A7` → `USB_D-`). `B6` and `B7` are no-connects. Tie A6–B6 and A7–B7 so a flipped cable still enumerates.

- [ ] **TMP36 `TEMP_SEN` is on GPIO47, which is not an ADC pin on the ESP32-S3** (ADC is GPIO1–20 only). U2 `VOUT` cannot be measured as drawn. Move `TEMP_SEN` to a freed ADC1 pin (same pin-budget problem as battery sense).

- [ ] **No board-level LiPo protection.** There is no DW01/FS8205 (or equivalent) in the schematic or BOM. J9 goes straight to TP4056 `BAT` and U13 `VIN`. Use a protected cell, or add a protection IC, so a dead pack cannot over-discharge.

## Should

- [ ] **Move signals off ESP32-S3 strapping pins.** These can disturb boot or JTAG if they are not in the expected state at reset:
  - BMA400 `INT1` (`BMA_INT`) → IO3
  - IR emitter (`IR_TX`) → IO45
  - IR receiver (`IR_RX`) → IO46
  (`BOOT` on IO0 and `EN`/SW2 reset are correct; leave them.)

- [ ] **SD card detect is not routed.** J6 pin 9 (`DET`) is a no-connect. SPI data pins are fine (DAT1/DAT2 unused is expected in 1-bit SPI). Wire `DET` (with a pull-up) to a spare expander GPIO if firmware should know a card is present without trying to mount it.

- [ ] **TCA9539 `~RESET` (U9 pin 24) is a no-connect.** A floating reset can glitch the expander. Pull `~RESET` to VDD, or drive it from a GPIO.

## Verify (do not respin on these until checked)

- TP4056 `CE` (pin 8) is not marked NC and appears wired; confirm it is held at VCC/`+5V` so charging can actually enable.
- TP4056 `TEMP` (pin 1) is wired, but there is no NTC on the BOM. Confirm the bias is in the valid window so the chip does not refuse to charge.
- No 1.8V LDO is on this schematic (the BOM still lists an XC6206 1.8V; the board LDO is U13 XC6220 3.3V). J1 (MAX30102) is powered from `+3V3_SEN` only. Confirm the module can run at 3.3V.
- I2C pull-ups R1/R3 (4.7k) are on always-on `+3.3V`, while IST8310 and the MAX30102 header sit on switched `+3V3_SEN`. Check for back-power when that rail is off.
- IST8310 `CAD0`/`CAD1` and `DRDY` are unconnected. Confirm the default I2C address is the one the firmware will use.
- J7 VBUS is visibly tied to `+5V` on `A4` only. Confirm the footprint shorts the other VBUS pins, or wire them too.
- Touch `RST` is the same net as `LCD_RST` (J5 pin 3 and J4 pin 6). Confirm the panel is OK with a shared reset. Touch `INT` is already on J5 pin 6 / `TCH_INT` / IO2 (firmware currently leaves the IRQ as `GPIO_NUM_NC` — that is a firmware fix, not a board change).

## Already fine (for this review)

- Haptic is a real load: M1 `Haptic_Motor`, Q1 AO3400A, D2 flyback, driven from `HAPTIC` (IO18).
- USB D+/D− land on the module USB-PHY pins (U3 pins 13/14).
- USB-C CC1/CC2 have 5.1k pull-downs to GND (R30, R31).
- I2C pull-ups are present (R1, R3).
- DS3231 is on I2C; `INT/SQW` goes to expander `RTC_INT`. `32KHZ` is unconnected (not required for timekeeping).
- No MPU6050 on this board, so no 0x68 clash with the DS3231.
- BMA400 `INT1` and MAX30102 `HR_INT` are routed (BMA to IO3, HR to the expander). The strapping issue on IO3 is listed above.
