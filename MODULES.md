# Module graph

Vulcan is a required dependency of this OS. Layout:

```
vulcan-lang                 BASE
  https://github.com/sera5m/vulcan-lang
  submodule: third_party/vulcan-lang

     +-- desktop VM         depends on BASE only
     |     vulcan-lang/vulcan_run.py
     |
     +-- watch VM           depends on BASE only
     |     THIS REPO: os_code/core/rs_vm
     |     baked into the firmware
     |
     `-- Vulcan IDE         depends on BASE + desktop VM
           vulcan-lang + desktop/vulcan_ide overlay
           not a firmware dependency
```

ESP-IDF compiles the watch VM only. It does not compile Python BASE or the IDE.
