# Vulcan on the watch (what actually runs)

There is **no JIT** on the ESP32-S3.

| Path | Where | On watch? |
|------|--------|-----------|
| Bytecode interpreter (`rsvm_compile` + `rsvm_run`) | `vm/rs_vm.cpp` | **yes** — this is the runtime |
| Parser (`.vul` → bytecode) | `vm/rs_vm_parse.cpp` | **yes** |
| Superops / trapdoor loops / opt passes | `vm/rs_vm_opt.cpp` | **yes** (bytecode rewrite, still interpreted) |
| Structure analysis | `vm/rs_vm_struct.cpp` | **yes** (analysis only) |
| `.bvul` load/save | same interpreter, skip parse | **yes** |
| AOT-C (`lang/codegen/rs_cgen` → gcc -O2) | zip / desktop only | **no** — no gcc on device |
| ikitaku “AI / thinking layer” | zip `ikitaku/` | **no** — not in firmware |

Comments that say “JIT-style” mean: emit `.bvul` then interpret that image. Same opcode loop, no native code.

## Firmware wiring

- Sources listed in root `CMakeLists.txt`
- `RSVM_IN_FIRMWARE=1` so `appManager::run_vulcan_script` / `feed_serial_source` call `rsvm_eval`
- UI runner: `os_code/applications/vulcanApp` (menu: “Vulcan VM”)
- Host GPIO/print: `vm/rs_vm_host_esp.cpp` (`rsvm_install_esp_host`)
- Include roots: `os_code/core/rs_vm` and `os_code/core/rs_vm/vm` (modules use `vm_modules/...`)

## Language that the interpreter understands

See `RS_VM_NOTES.md`. Short version used by the on-watch sample:

```vulcan
set_step_depth 50000;
fn greet in[] out[] {
  print("hi");
  return;
}
fn main in[] out[] {
  greet in[];
  i32 i = 0;
  do n[5] { i = i + 1; }
  print(i);
  return;
}@monitor_time_highres;
```

Call style is `name in[...] out[...]` (`out[here]` leaves a value on the stack). Bare `name()` is unreliable in this parser.

`do n[N]` uses a trapdoor (`OP_TRAP_LOOP`) by default — a C `for` over the body range inside the interpreter, not a JIT.

## Still not done (on purpose, this pass)

- UART `RSDOM_TYPE_VM` / `VM_SRC` receive → `rsvm_eval` (collective / puppet exec)
- `host.uart_send` hook in `rsvm_install_esp_host`
- Loading `.vul` from SD
- ikitaku
- gcc AOT
