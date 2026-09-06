# OIRIA OS (ESP-IDF)

Smartwatch firmware. The language it runs is **Vulcan**.

## Required dependency

Vulcan is baked into the OS (`os_code/core/rs_vm`). The desktop/reference tree is a separate repo so other projects can use the same language without this firmware:

**https://github.com/sera5m/vulcan-lang**

```bash
git clone --recurse-submodules https://github.com/sera5m/OIRIA_OS_espIDF
# or after a plain clone:
git submodule update --init --recursive
```

If you do not use submodules, clone vulcan-lang next to this tree:

```bash
git clone https://github.com/sera5m/vulcan-lang third_party/vulcan-lang
```

See `VULCAN.md`.
