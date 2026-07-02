#include "rshell_streamdefs.h"
#include <string.h>
#include "esp_log.h"

static const char* TAG = "StreamDefs";

static const StreamCompatEntry g_stream_compat_map[] = {
    {"TemperatureSensor", SOURCE_POLLED_SENSOR, COMPAT_CAN_BRANCH, PERIPH_ADC | PERIPH_I2C, RATE_LOW, (1<<SINK_SCREEN) | (1<<SINK_STORAGE) | (1<<SINK_DUMP) },
    {"KeystrokeInput", SOURCE_INPUT_SYSTEM, COMPAT_CAN_BRANCH | COMPAT_LOW_LATENCY, PERIPH_USB | PERIPH_GPIO, RATE_HIGH, (1<<SINK_SCREEN) | (1<<SINK_USB_KEYBOARD) | (1<<SINK_OTHER_APPLICATION) },
    {"SDCardRead", SOURCE_STORAGE, COMPAT_NEEDS_CBOR, PERIPH_SDMMC, RATE_MEDIUM, (1<<SINK_SCREEN) | (1<<SINK_REMOTE_SCREEN) | (1<<SINK_DUMP) },
    {"AudioCapture", SOURCE_AUDIO_IN, COMPAT_HIGH_BANDWIDTH, PERIPH_AUDIO_I2S, RATE_HIGH, (1<<SINK_AUDIO) | (1<<SINK_STORAGE) },
    {"CameraFrame", SOURCE_VIDEO_CAMERA, COMPAT_HIGH_BANDWIDTH, PERIPH_CAMERA | PERIPH_DMA, RATE_CRITICAL, (1<<SINK_SCREEN) | (1<<SINK_REMOTE_SCREEN) | (1<<SINK_STORAGE) },
    {NULL, 0, 0, 0, 0, 0}
};

const StreamCompatEntry* stream_get_compat(const char* name) {
    if (!name) return NULL;
    for (int i = 0; g_stream_compat_map[i].name != NULL; i++) {
        if (strcmp(g_stream_compat_map[i].name, name) == 0) {
            return &g_stream_compat_map[i];
        }
    }
    return NULL;
}

struct DataItem* dataitem_new(SourceType src, size_t payload_size) {
    DataItem* item = (DataItem*)malloc(sizeof(DataItem));
    if (item) {
        memset(item, 0, sizeof(DataItem));
        item->source_type = src;
        item->timestamp = esp_timer_get_time();
        if (payload_size > 0) {
            item->payload = (uint8_t*)malloc(payload_size);
            item->payload_len = payload_size;
        }
    }
    return item;
}

void dataitem_free(DataItem* item) {
    if (item) {
        if (item->metadata_cbor) free(item->metadata_cbor);
        if (item->payload) free(item->payload);
        free(item);
    }
}

bool dataitem_add_cbor_tag(DataItem* item, const char* key, const char* value) {
    // Stub - implement with actual CBOR lib later
    ESP_LOGI(TAG, "Tag added (stub): %s = %s", key, value);
    return true;
}

// Stub implementations - expand as needed
void datastream_init(void) {
    ESP_LOGI(TAG, "Datastream initialized");
}

void datastream_pump_start(void) {
    // Call your pump start
    ESP_LOGI(TAG, "Pump started");
}

bool datastream_push_input(const InputEvent* ev) {
    // Bridge to pump
    return false; // TODO
}

bool sink_register(SinkType type, SinkHandler handler, void* ctx) {
    return true; // stub
}

void sink_screen(DataItem* item, void* ctx) {}
void sink_storage_cbor(DataItem* item, void* ctx) {}
void sink_hid(DataItem* item, void* ctx) {}