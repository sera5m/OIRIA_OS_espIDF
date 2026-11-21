# Pipes and pools vs Unix (V7 / xv6 / Linux)

Short comparison so the next person does not reinvent the wrong half.

## What Unix actually does

V7 `pipe()` (and xv6, and Linux `pipe(7)`):

- One pipe object + two file structures (read end, write end).
- Reference count on the pipe starts at 2. `dup` / `fork` bump it.
- Circular byte buffer (512 B in V7/xv6; page-based circular in modern Linux).
- Reader sleeps if empty. Writer sleeps if full. Non-blocking is an option.
- Last writer close → readers see EOF (read returns 0).
- Last reader close → writers get EPIPE / SIGPIPE.
- Writes ≤ PIPE_BUF are atomic.
- Ownership is the open file descriptors, not a global mode bit.
- Fan-out and merge are **not** kernel pipe features. They are composition
  (`tee`, multiple writers into one pipe, user-space).

Shared memory is a separate mechanism (shm / mmap MAP_SHARED) with attach,
detach, and permissions. It is not a pipe.

## What OIRIA has

- `DataPool` — software shared buffer + token acquire/release. Closer to shm
  than to a pipe. No MMU; isolation is by convention plus the owner identity.
- `RshellPipe` — flow graph (direct / fan / merge / clone / c_fan / c_merge)
  with three transports (InterDevice, InterProcess, InterBuffer).
- `InterProcessPipe` — FreeRTOS queue of `DataItem*`. A message queue, not a
  Unix byte pipe. Now carries Unix-style open counts (read_open / write_open),
  EOF when the last writer has closed and the queue is empty, and broken-pipe
  when the last reader has closed.
- Fan / merge stay in the flow graph. That matches Unix: the kernel pipe is
  the transport; composition is above it.

## What we deliberately do not copy

- Per-process address spaces / page tables. ESP32-S3 has no usable MMU for
  this. Software isolation only.
- SIGPIPE. We return false from `push` and log. Callers decide.
- A 512-byte circular byte buffer. `DataItem` is the unit. A byte pipe can
  sit on top later if a path needs it.
- Blocking as the default. `push` stays non-blocking (semi-async). `pop_wait`
  is available when a consumer wants to sleep.

## Mapping

| Unix                         | OIRIA                                      |
|------------------------------|--------------------------------------------|
| pipe inode + two file ends   | InterProcessPipe + read_open / write_open  |
| circular buffer              | FreeRTOS queue of DataItem*                |
| read blocks if empty         | pop_wait(timeout)                          |
| last writer close → EOF      | eof() after close_write and drain          |
| last reader close → EPIPE    | push() returns false (broken())            |
| shm attach/detach            | DataPool::attach / detach                  |
| tee / multiple writers       | fan / c_fan / merge in the flow graph      |
| capabilities (Linux)         | AppCapability (declared, not enforced)     |
