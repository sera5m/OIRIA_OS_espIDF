# rShell Datastreamer + App Framework Architecture

**Overview**  
A unified, flexible dataflow system for the ESP32-S3 OS. It combines:
- Unix-like pipes + modern dataflow (sources → pipes/filters → sinks)
- Direct PSRAM sharing for high-performance bulk data
- App-centric design (`AppBase` / `appManager`)
- CBOR tagging for rich metadata
- Integration with existing input, notifications, storage (`d_sdc`), HID, etc.

**Core Currency**: `DataItem` (timestamp + source info + CBOR tags + payload)

---

## Core Files & Responsibilities

### `rshell_streamdefs.h` + `.c`
**Purpose**: Core type definitions (C-friendly for broad use).

- Defines `SourceType`, `SinkType`, `PeripheralMask`, `CompatFlags`, `DataItem`, `PipeType`, `FilterType`.
- Compatibility map (`g_stream_compat_map`) for routing/automation decisions.
- Basic API: `dataitem_new/free`, `datastream_push_input`, `sink_register`, etc.
- **Current State** (latest send): Cleaned, extern "C", forward declarations for C++ interop.

**Intention**: Foundation. Expand compat map and add more filter/sink helpers.

---

### `rshell_pump.hpp` + `.cpp`
**Purpose**: Central "pump" task + router.

- FreeRTOS task that pulls `DataItem`s from queue.
- Routes using compat map + tags.
- Calls registered sinks (screen, storage, HID, etc.).
- Supports fan-out, filters, etc.

**Current State**: Basic skeleton present. Needs full routing logic and CBOR storage sink.

**Intention**: Smart dispatcher. Can run at medium priority. High-rate paths bypass via PSRAM rings.

---

### Input System (`input_handler.hpp/cpp`, `ipt_x.cpp/hpp`, `hid_t.h`)
**Purpose**: Hardware input (buttons, knobs, USB HID, Rubber Ducky).

- Produces `InputEvent`.
- Pushes to `ProcInputQueTarget` **and** `datastream_push_input()` (via new `HIDTarget::toStreamCore*` cases).

**Current State**: Well-developed. Bridge to streamer added in `ipt_x.cpp`.

**Intention**: All input becomes `DataItem`s for unified handling (screen, logging, apps, HID passthrough).

---

### App Framework (`s_hell.hpp/cpp`, `defaultAppList.hpp`, `env_vars.h/c`)
**Purpose**: Task lifecycle, focus management, app registry.

- `AppBase`: Base for all apps. Added `on_stream_data()` and `publish_data()`.
- `appManager`: Registers, launches, routes input, focus.
- New capabilities: `STREAM_IN_CAPABLE`, `ST_RING_CAPABLE`, etc.
- Pipe/outlet helpers stubbed.

**Current State**: Strong, with streaming extensions added.

**Intention**: Apps become both sources and sinks. Direct PSRAM sharing for performance-critical apps.

---

### Notifications (`rs_notif_dispatcher.h/c`)
**Purpose**: Alerts, alarms, timers.

**Current State**: Basic local + ULP stubs.

**Intention**: Treat as special sink/source. Route via streamer for unified logging/display.

---

### Storage & FS (`d_sdc.h/c`)
**Purpose**: SD card, OFV_Mode (file type hints).

**Intention**: Primary sink for `SINK_STORAGE` with CBOR serialization + extension mapping.

---

### PSRAM Helpers (`psram_std.hpp`)
**Purpose**: Safe PSRAM allocation.

**Intention**: Used for ring buffers and large shared data.

---

## Data Flow Patterns

1. **High-Performance Bulk** (sensors, audio, frames):
   - Source → PSRAM Ring Buffer → Consumer App/Task (direct).

2. **Smart Routed** (events, logs, mixed):
   - Source → Pump → Filters → Multiple Sinks (fan-out).

3. **App-to-App**:
   - `publish_data()` → Pump or direct ring.
   - `on_stream_data()` callback.

4. **Input Special Case**:
   - Hardware → `InputEvent` → both legacy paths **and** streamer.

---

## Key Design Decisions

- **C/C++ Interop**: Core defs in C (`extern "C"`), C++ wrappers where needed.
- **Performance**: Direct PSRAM rings for hot paths; pump for intelligence.
- **Extensibility**: Compat map + dynamic sink registration + app capabilities.
- **CBOR**: Metadata + storage boundary (not every real-time packet).
- **Notifications**: Will become a sink (and possibly source for alarms).

---

## TODO 

1. Complete `rshell_pump.cpp` routing + basic sinks.
2. Implement PSRAM ring template.
3. Add real CBOR (qcbor) helpers.
4. Wire notifications as sink.
5. Test end-to-end: Knob → Streamer → Screen + Log to SD.

---
