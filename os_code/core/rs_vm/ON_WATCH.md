# Toplethos on the watch (what actually runs)

There is **no JIT** on the ESP32-S3.

| Path | Where | On watch? |
|------|--------|-----------|
| Bytecode interpreter (`rsvm_compile` + `rsvm_run`) | `vm/rs_vm.cpp` | **yes** — this is the runtime |
| Parser (`.vul` → bytecode) | `vm/rs_vm_parse.cpp` | **yes** |
| Superops / trapdoor loops | `vm/rs_vm.cpp` + parse emit `OP_TRAP_LOOP` | **yes** (bytecode rewrite, still interpreted) |
| Structure tables | `vm/rs_vm.cpp` (`rsvm_struct_by_name`) | **yes** |
| `.bvul` load/save | same interpreter, skip parse | **yes** |
| AOT-C (`lang/codegen/rs_cgen` → gcc -O2) | zip / desktop only | **no** — no gcc on device |
| ikitaku “AI / thinking layer” | zip `ikitaku/` | **no** — not in firmware |

Comments that say “JIT-style” mean: emit `.bvul` then interpret that image. Same opcode loop, no native code.

## Firmware wiring

- Sources listed in root `CMakeLists.txt`
- `RSVM_IN_FIRMWARE=1` so `appManager::run_toplethos_script` / `feed_serial_source` call `rsvm_eval`
- UI runner: `os_code/applications/toplethosApp` (menu: “Toplethos VM”)
- Host GPIO/print: `vm/rs_vm_host_esp.cpp` (`rsvm_install_esp_host`)
- Include roots: `os_code/core/rs_vm` and `os_code/core/rs_vm/vm` (modules use `vm_modules/...`)

## Language that the interpreter understands

See `RS_VM_NOTES.md`. Short version used by the on-watch sample:

```toplethos
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


## Language 1.0 on the watch

The device is not the desktop. It runs the forms below. Anything else fails at parse time instead of guessing.

| 1.0 form | on the watch |
|---|---|
| `fn add in[i32 a] out[i32] { return a + 1; }` | Yes. `out[i32]` is a result left on the stack. `add in[1]` uses it. |
| `out[name]` | The older named output. Still works. |
| `sin in[90]` and `sin(90)` | Both. `cos`, `tan`, and `sin_amp` too. |
| `&&` `||` | Yes. The right side does not run when the left decides it. |
| `object` | The same layout as `struct`. `@childof[Base]` is the parent. |
| `namespace Name { ... }` | The brace is parsed. Names are not prefixed. |
| `i32` `ui32` `f32` `bf16` `t8` and the other atoms | They parse. Every one is the same cell. There is no `bf16` unit and no ternary unit. |
| `u8` `u16` `u32` `u64` `uf16` | Older names. Still accepted. 1.0 spells these `ui8` and `uf32`. |
| `@parallel[n]` | CPU threads only, and the watch cap is 1. |
| `@gpuparallel` `@npuparallel` | Parse error: `not on the watch`. |
| `rng` `match` `flow` `^=` | Parse error. `FlowMap` is still the watch form. |

`print(...)`, `native(...)`, and `sh(...)` stay parenthesized. Those are host calls, not 1.0 functions.
