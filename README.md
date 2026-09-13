# OIRIA OS (ESP-IDF)

Smartwatch firmware. The language it runs is **Vulcan**.

**Build:** this repo **is** the ESP-IDF project. See [BUILD.md](BUILD.md).

```bash
. ~/esp/esp-idf/export.sh
git clone https://github.com/sera5m/OIRIA_OS_espIDF
cd OIRIA_OS_espIDF
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Do **not** dump this tree into `tusb_hid/main/`. That old wrapper is gone.

Vulcan on the watch is `os_code/core/rs_vm/` (compiled in). The desktop/reference
tree is separate and **not** required to flash:

**https://github.com/sera5m/vulcan-lang**

```bash
git submodule update --init --recursive   # optional, fills third_party/vulcan-lang
```

See `VULCAN.md`, `SIGGEN.md`, `BUILD.md`.
