#pragma once

#ifdef __cplusplus
extern "C" {
#endif



#include "d_sdc.h"  // For OFV_Mode + extension mapping (storage type hints)
#include <stdint.h>
#include <stdbool.h>

// Forward declare InputEvent without pulling in full C++ header
typedef struct InputEvent InputEvent;

// Forward declarations (implement in .c or pump task)
struct DataItem;  // Defined below or in separate header

// =============================================
// SOURCE TYPES
// =============================================
typedef enum {
    SOURCE_NULL = 0,
    SOURCE_POLLED_SENSOR,      // e.g. temp/humidity that needs periodic read()
    SOURCE_NOTIFY_SENSOR,      // interrupt / event driven (e.g. GPIO, I2C slave)
    SOURCE_DATA_GENERATOR_TASK, // FreeRTOS task producing data (e.g. signal gen)
    SOURCE_RETRIEVAL_FROM_RAM, // PSRAM / internal RAM buffer read
    SOURCE_STORAGE,            // SD card read (uses d_sdc + OFV)
    SOURCE_USB,                // USB CDC / HID input
    SOURCE_INPUT_SYSTEM,       // Keystrokes, touch, etc. (integrates with terminal)
    SOURCE_I2C_RAW,
    SOURCE_SPI_RAW,
    SOURCE_WIRELESS,           // WiFi/BT data (abstracted)
    SOURCE_OPTICAL,
    SOURCE_AUDIO_IN,
    SOURCE_VIDEO_CAMERA,
    SOURCE_COUNT
} SourceType;

// =============================================
// SINK TYPES
// =============================================
typedef enum {
    SINK_DUMP = 0,             // yeet / discard (debug)
    SINK_OTHER_APPLICATION,    // inter-core / IPC to another task/app
    SINK_STORAGE,              // microSD (CBOR serialized + OFV_Mode hint)
    SINK_SCREEN,               // local display / terminal emulator
    SINK_REMOTE_SCREEN,        // cast to remote terminal
    SINK_AUDIO,
    SINK_LED,
    SINK_OPTICAL,
    SINK_REMOTE_DEVICE,
    SINK_HARDWARE_SIGNAL_GEN,
    SINK_WIRELESS,
    SINK_USB,
    SINK_USB_KEYBOARD,
    SINK_ARBITRARY,
    SINK_COUNT
} SinkType;

// =============================================
// PERIPHERALS (for automation / capability map)
// =============================================
// Bitmask so a source/sink can declare what hardware it can drive/uses.
// Pump task can query this to partially auto-configure (save cycles).
typedef enum {
    PERIPH_NONE       = 0,
    PERIPH_UART       = (1 << 0),
    PERIPH_I2C        = (1 << 1),
    PERIPH_SPI        = (1 << 2),
    PERIPH_ADC        = (1 << 3),
    PERIPH_GPIO       = (1 << 4),
    PERIPH_TIMER      = (1 << 5),
    PERIPH_DMA        = (1 << 6),   // for high-rate transfers
    PERIPH_USB        = (1 << 7),
    PERIPH_WIFI       = (1 << 8),
    PERIPH_BT         = (1 << 9),
    PERIPH_SDMMC      = (1 << 10),  // ties into d_sdc
    PERIPH_LED_PWM    = (1 << 11),
    PERIPH_AUDIO_I2S  = (1 << 12),
    PERIPH_CAMERA     = (1 << 13)

    // Add more as needed
} PeripheralMask;

// =============================================
// COMPATIBILITY / AUTOMATION PROPERTIES
// =============================================
// Simple bitflags for compatibility map
typedef enum {
    COMPAT_CAN_BRANCH     = (1 << 0),  // fan-out allowed
    COMPAT_CAN_FAN_IN     = (1 << 1),
    COMPAT_SYNC_REQUIRED  = (1 << 2),  // needs synchronization barrier
    COMPAT_HIGH_BANDWIDTH = (1 << 3),
    COMPAT_LOW_LATENCY    = (1 << 4),
    COMPAT_NEEDS_CBOR     = (1 << 5),  // must serialize before sink
} CompatFlags;

// Action / priority rate hint (for pump task scheduling)
typedef enum {
    RATE_CRITICAL = 0,   // highest freq / priority
    RATE_HIGH,
    RATE_MEDIUM,
    RATE_LOW,
    RATE_BACKGROUND
} ActionRate;

// =============================================
// DATA ITEM (tagged stream unit)
// =============================================
typedef struct DataItem {
    uint64_t timestamp;           // esp_timer or FreeRTOS tick
    SourceType source_type;
    uint32_t source_id;           // unique per source instance
    uint32_t compat_flags;        // CompatFlags bitmask
    PeripheralMask periph_mask;   // hardware this item relates to (automation)
    
    // Tags as CBOR (compact + extensible)
    // In practice: encode a map with keys like "temp", "unit", "displayable", etc.
    // Pump can add/inspect tags easily.
    size_t metadata_len;
    uint8_t* metadata_cbor;       // owned or view; small for embedded
    
    size_t payload_len;
    uint8_t* payload;             // owned, reference counted, or zero-copy view
    
    // Optional hint for storage sinks (integrates with your FS)
    OFV_Mode storage_hint;

    union {
        void* input_ev_ptr;     // points to InputEvent in C++
    } specific;
} DataItem;

typedef enum {
    PIPE_QUEUE,           // FreeRTOS queue
    PIPE_PSRAM_RING,      // high speed lock-free-ish
    PIPE_DIRECT,          // inline call (low latency)
} PipeType;

typedef enum {
    FILTER_NONE,
    FILTER_DEBOUNCE,
    FILTER_AVERAGE,
    FILTER_TAG_ENRICH,
    FILTER_CONVERT_FORMAT,
    // etc.
} FilterType;

typedef void (*SinkHandler)(struct DataItem* item, void* user_ctx);

// =============================================
// PUBLIC API
// =============================================
struct DataItem* dataitem_new(SourceType src, size_t payload_size);
void dataitem_free(struct DataItem* item);
bool dataitem_add_cbor_tag(struct DataItem* item, const char* key, const char* value);

void datastream_init(void);
void datastream_pump_start(void);

bool datastream_push_input(const InputEvent* ev);   // main bridge from your input_task
void sink_notification(struct DataItem* item, void* ctx);
bool sink_register(SinkType type, SinkHandler handler, void* ctx);

// Built-in sink examples
void sink_screen(struct DataItem* item, void* ctx);
void sink_storage_cbor(struct DataItem* item, void* ctx);
void sink_hid(struct DataItem* item, void* ctx);


// =============================================
// COMPATIBILITY MAP TABLE
// =============================================
// Example static mapping. Expand as needed.
// Pump task / router uses this (or a dynamic registry) to decide routing + automation.
typedef struct {
    const char* name;           // human readable
    SourceType src_type;
    uint32_t compat;            // CompatFlags
    PeripheralMask periph;      // automation hint
    ActionRate rate;
    uint32_t can_sink_to;       // bitmask of SinkType (1<<SINK_xxx)
    // Future: more props (min_interval_ms, etc.)
} StreamCompatEntry;


// Utility to lookup compat
const StreamCompatEntry* stream_get_compat(const char* name);
// bool stream_is_compatible(SourceType src, SinkType sink);

// =============================================
// PUMP TASK INTERFACE THEORY (FreeRTOS)
// =============================================
// The "pump" is a dedicated FreeRTOS task that moves data.
// It can use the compat map for partial automation (e.g. auto-init periphs, schedule rates).
// to keep it simple, the following sort of thing is in the pump task file
/*
void datastream_pump_task(void* param) {
    while(1) {
        // poll sources based on rate / compat
        // produce DataItem
        // route via tags + compat map to sinks
        // for storage: use d_sdc + CBOR encode + OFV hint
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
*/

// Router / Broker API (declare here, implement elsewhere)
void datastream_route(struct DataItem* item);  // fan-out based on tags + compat

// Init (call early, e.g. app_main)
void datastream_init(void);  // registers default sources/sinks, starts pump task


#ifdef __cplusplus
}
#endif
