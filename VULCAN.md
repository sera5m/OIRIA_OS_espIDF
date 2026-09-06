# Vulcan is a required dependency of this OS

The watch does not have a "scripting add-on". Vulcan *is* how user code runs on device.

| Layer | Where | Role |
|-------|--------|------|
| Language + desktop runner/IDE | [sera5m/vulcan-lang](https://github.com/sera5m/vulcan-lang) | spec-by-implementation, PC interpreter, GTK IDE |
| Firmware VM | `os_code/core/rs_vm/` | same language, C interpreter on ESP32-S3 |
| This tree's desktop overlay | `desktop/vulcan_ide/` | copy used while developing against USB |

Shared ideas (opcodes, LUT trig, `@latex_internal`, tyrant/puppet frames) must stay aligned across the two repos. Change the language in **vulcan-lang** first when you can; port the C VM here.

## Get it

```bash
git submodule add https://github.com/sera5m/vulcan-lang third_party/vulcan-lang
git submodule update --init
```

`third_party/vulcan-lang` is not compiled by ESP-IDF (Python). Firmware still builds `os_code/core/rs_vm`. The submodule is required so the language and the OS stay one product.
