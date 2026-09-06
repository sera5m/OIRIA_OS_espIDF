# Watch VM implementation

BASE libs (parse, bytecode, opcodes): `os_code/core/rs_vm/vm/`
Same files desktop links via `vulcan-lang/cpp_vm` + `-DOIRIA_ROOT`.

Local to the watch only:
- `rs_vm_host_esp.cpp`
- IDF CMake, GPIO, LCD, UART

Desktop host is `vulcan-lang/cpp_vm/rsvm_host_desktop.cpp`.
Do not fork parse/opcodes for the watch.
