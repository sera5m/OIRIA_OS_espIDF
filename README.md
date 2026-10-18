# OIRIA OS (ESP-IDF)

Smartwatch firmware. The language it runs is **Toplethos**.

**Build:** this repo **is** the ESP-IDF project. See [BUILD.md](BUILD.md).

```bash
. ~/esp/esp-idf/export.sh
git clone https://github.com/sera5m/OIRIA_OS_espIDF
cd OIRIA_OS_espIDF
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Do **not** dump this tree into `tusb_hid/main/`. That old wrapper is gone.

Toplethos on the watch is `os_code/core/rs_vm/` (compiled in). The desktop/reference
tree is separate and **not** required to flash:

**https://github.com/sera5m/toplethos-lang**

```bash
git submodule update --init --recursive   # optional, fills third_party/toplethos-lang
```

See `TOPLETHOS.md`, `SIGGEN.md`, `BUILD.md`.
