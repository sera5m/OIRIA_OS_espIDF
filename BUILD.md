# Fresh machine → `idf.py build` → watch

Git is now a full ESP-IDF **project**. Clone it and build from the repo root.
Do **not** copy this tree into `examples/.../tusb_hid/main/` — that was the old
hierarchy and it is what you just lost.

Vulcan on device is already in `os_code/core/rs_vm/`. You do **not** need
`vulcan-lang` for firmware.

## 0. Chip

ESP32-S3. `sdkconfig` already has `CONFIG_IDF_TARGET="esp32s3"`.

## 1. ESP-IDF (once per computer)

EndeavourOS / Arch, matching the `sdkconfig` (IDF 5.5.x):

```bash
sudo pacman -S --needed git wget flex bison gperf python python-pip \
    python-setuptools cmake ninja ccache dfu-util libusb python-virtualenv \
    python-pyserial

mkdir -p ~/esp
cd ~/esp
git clone -b v5.5.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
```

Every new terminal:

```bash
. ~/esp/esp-idf/export.sh
```

(Put that line in `~/.bashrc` if you want.)

## 2. Firmware

```bash
. ~/esp/esp-idf/export.sh
cd ~/Desktop/devprojects/smartwatch    # or wherever you keep it
rm -rf OIRIA_OS_espIDF                 # only if the old tree is junk
git clone https://github.com/sera5m/OIRIA_OS_espIDF
cd OIRIA_OS_espIDF

idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Port is often `/dev/ttyACM0` or `/dev/ttyUSB0`. `ls /dev/ttyACM* /dev/ttyUSB*` if unsure.
On EndeavourOS you need the `uucp` group (log out/in once):

```bash
sudo usermod -aG uucp "$USER"
```

`idf.py fullclean` if CMake cached the broken layout.

## 3. What should come up

- Watch face / menu on the LCD
- Utilities → **Vulcan VM**, **SigGen**, **Scope**
- Wi‑Fi STA from `os_code/core/com/autoconnect.conf`, else AP `OIRIA-vulcan` / `vulcanvulcan`
- Browser: `http://<watch-ip>/`  (Script / Wave / Scope)

## 4. Desktop Vulcan (optional, PC only)

Not used by `idf.py`. Only if you want to run `.vul` on the laptop:

```bash
sudo pacman -S --needed python-gobject gtk4 python-matplotlib
git clone https://github.com/sera5m/vulcan-lang
git clone https://github.com/sera5m/vulcan-ide
cd vulcan-ide
PYTHONPATH=../vulcan-lang:. python3 vulcan_ide.py
```

Or just: `python3 vulcan-lang/vulcan_run.py vulcan-lang/examples/sin_demo.vul`

## Layout (so you don’t nest it again)

```
OIRIA_OS_espIDF/          ← cd here, idf.py build
  CMakeLists.txt          ← project()
  sdkconfig
  main/CMakeLists.txt     ← idf_component_register, SRCS are ../os_code/...
  tusb_hid_example_main.cpp
  os_code/                ← VM, apps, rShell
  code_stuff/signal_gen/
```
