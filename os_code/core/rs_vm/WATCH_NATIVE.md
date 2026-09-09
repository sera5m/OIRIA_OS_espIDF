# Watch native + collective Vulcan terminal

Vulcan is the bash analogue. The second chip **listens** on UART1.

## Roles (`boot_role`)

| Role | LCD | Apps | UART |
|------|-----|------|------|
| SOLO | yes | yes | listen (if a peer is wired) |
| TYRANT | yes | thin | listen DOM + can TX VM blobs |
| PUPPET | no | yes | listen VM, TX DOM |

`env("ROLE")` / `sh("role")` prints the name. `v_env` is `env_vars.h` `EnvConfig`.

## Storage

- Files: `/sdcard/…` (`d_sdc` + VFS)
- `cwd` starts at `/sdcard`
- `pool(name, op, data)` → `/sdcard/rpool/name.rpool` (app/kernel reserved *file* space)

## Send blob vs stream

**Blob** (whole `.vul`, like scp + bash):

```
RSDOM type 0x11  payload = source
```

or ASCII: `run /sdcard/scripts/foo.vul`

**Stream** (interrupt slave, sequential cmds):

```
start sequence:
print(ls("."));
open_app("Snake");
end sequence.
```

or `<<VUL` / `VUL>>`. Binary: 0x15 / 0x16 / 0x17.

Frames interrupt the other ESP32 over UART1 (GPIO7/18 @ 921600).
