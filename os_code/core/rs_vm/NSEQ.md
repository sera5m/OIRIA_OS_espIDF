# native_seq — trapdoored on-device array action

`native("wave", …)` is one interpreter op **plus** a string lookup, **plus**
(on tyrant) a sprintf of Vulcan source sent over UART. A burst of twelve
calls is twelve translates.

`native_seq` packs those calls into an array of interned nids and walks it
in **C**, like `do n[N]` uses `OP_TRAP_LOOP`.

```vulcan
print(native_seq(
  native("wave", 1, 1000, 50, 4, 80),
  native("delay", 200),
  native("adc", 1),
  native("wave_stop")
));
```

Compile-time args must be constants (the trapdoor cannot wait on the
interpreter). Runtime values go in a packed `i32` array, stride 7:

```
[nid, nargs, a0, a1, a2, a3, a4] × N
```

```vulcan
i32 burst[14];
burst[0] = 1;  burst[1] = 5; burst[2] = 1; burst[3] = 1000;
burst[4] = 50; burst[5] = 4; burst[6] = 80;
burst[7] = 2;  // wave_stop
native_seq(burst);
```

## nids

| id | name |
|----|------|
| 0 | nop |
| 1 | wave |
| 2 | wave_stop |
| 3 | wave_freq |
| 4 | wave_duty |
| 5 | wave_amp |
| 6 | sweep |
| 7 | adc |
| 8 | scope |
| 9 | delay (ms) |
| 10 | gpio_wr |
| 11 | gpio_rd |
| 12 | pin_mode |
| 13 | dig_wr |
| 14 | dig_rd |

Delay / GPIO / ADC hit the existing host hooks directly (no `strcmp`).
Wave / sweep go through `host.native_id`.

## UART

Blob magic `NSQ1` (`RSDOM_TYPE_NSEQ = 0x18`). Tyrant sends **one** frame.
Puppet runs the C trampoline — it does **not** re-parse `.vul`.

Opcode `RSVM_OP_NATIVE_SEQ = 0xF2`.
