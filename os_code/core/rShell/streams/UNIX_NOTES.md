# Pipes and pools vs Unix (V7 / xv6 / Linux)

Short comparison so the next person does not reinvent the wrong half.

## What Unix actually does

V7 `pipe()` (and xv6, and Linux `pipe(7)`):

- One pipe object + two file structures (read end, write end).
- Reference count on the pipe starts at 2. `dup` / `fork` bump it.
- Circular byte buffer (512 B in V7/xv6; page-based circular in modern Linux).
- Reader sleeps if empty. Writer sleeps if full. Non-blocking is an option.
- Last writer close leads to EOF (read returns 0).
- Last reader close leads to EPIPE. No SIGPIPE here.
- Writes of at most PIPE_BUF are atomic.
- Ownership is the open file descriptors, not a global mode bit.
- Fan-out and merge are composition, not a kernel pipe feature.

Shared memory is a separate mechanism. It is not a pipe.

## What OIRIA has

- `DataPool` is a software shared buffer plus a token. Closer to shm than to a pipe.
- `RshellPipe` is the flow graph (direct / fan / merge / clone / c_fan / c_merge) with three transports.
- `InterProcessPipe` is a FreeRTOS queue of `DataItem*`. Open counts, EOF, broken pipe.
- Fan and merge stay in the flow graph.

## What we deliberately do not copy

- Page tables. Software isolation only.
- SIGPIPE. `push` returns false.
- A 512-byte byte pipe. `DataItem` is the unit.
- Blocking as the default. `push` stays non-blocking. `pop_wait` is the sleeper.
- A POSIX read/write mode on rings. Rings stay sized push/pop.

## Behavior that matches the table

- `PoolRing` wraps a DataPool ring. MICROSD / NVS / EXT_NONVOL keep a PSRAM working buffer and persist via `.rpool`.
- `set_atomic_small` refuses a payload above 512 bytes. Smaller pushes are one locked copy or one queue send.
- `reader_pausing` is set while `pop_wait` blocks. `writer_pausing` stays set when a non-blocking push finds the queue full, and clears on the next successful send.
- Capabilities are checked at use. Hard-storage pools need `USES_SD_CARD`. Creating a pipe needs `STREAM_OUT_CAPABLE`. GPIO, wireless, and wake have `request_*` helpers.
- Last `close_write` pushes a null sentinel so `pop_wait(portMAX_DELAY)` wakes and sees EOF.
- The pump's ring token is `DataPool::system_writer()`, not the string "pump".
- Input events are copied into the DataItem payload. `input_ev_ptr` aims at that copy.
- `.rpool` magic is the original UTF-8 marker, compared with `sizeof`, and a ring file also stores head, tail, and count.
- Ring write index is `tail + count`. `head` tracks the read index.

See also `os_code/core/STRUCTURE.md`.

| Unix | OIRIA |
| --- | --- |
| pipe inode + two file ends | InterProcessPipe + read_open / write_open |
| circular buffer | FreeRTOS queue of DataItem* |
| read blocks if empty | pop_wait(timeout) |
| last writer close, then EOF | eof() after close_write and drain |
| last reader close, then EPIPE | push() returns false (broken()) |
| shm attach/detach | DataPool::attach / detach |
| tee / multiple writers | fan / c_fan / merge in the flow graph |
| capabilities | AppCapability, checked at use |
