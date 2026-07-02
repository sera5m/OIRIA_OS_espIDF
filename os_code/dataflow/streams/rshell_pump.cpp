#include "rshell_pump.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "stdpsram.hpp"   // your PSRAM helpers
#include "d_sdc.h"        // for storage sink
#include "s_hell.hpp"
static const char* TAG = "DataPump";



// Main queue
QueueHandle_t DataStreamerPump::event_queue = nullptr;

void DataStreamerPump::start() {
    if (!event_queue) {
        event_queue = xQueueCreate(64, sizeof(DataItem*));
    }
    // Init PSRAM ring
    if (!psram::g_ring) {
        psram::g_ring = new (heap_caps_malloc(sizeof(psram::EventRingBuffer), MALLOC_CAP_SPIRAM)) psram::EventRingBuffer();
    }

    xTaskCreatePinnedToCore(pump_task, "datastream_pump", 8192, nullptr, 2, nullptr, 0);  // core 0 or 1
    ESP_LOGI(TAG, "DataStreamer pump started");
}

bool DataStreamerPump::pushInputEvent(const InputEvent& ev) {
    DataItem* item = dataitem_new(SOURCE_INPUT_SYSTEM, 0);
    if (!item) return false;

    item->specific.input_ev = ev;
    item->timestamp = ev.timestamp;
    item->compat_flags = COMPAT_CAN_BRANCH | COMPAT_LOW_LATENCY;
    item->periph_mask = PERIPH_USB | PERIPH_GPIO;  // from compat map

    // Add basic CBOR tag example
    // dataitem_tag_cbor(item, "input_type", ev.source_device_type);

    return pushDataItem(item);
}

bool DataStreamerPump::pushDataItem(DataItem* item) {
    if (!item || !event_queue) return false;
    return xQueueSend(event_queue, &item, 0) == pdTRUE;
}

static void pump_task(void* param) {
    ESP_LOGI(TAG, "Pump task running");
    DataItem* item = nullptr;

    while (true) {
        if (xQueueReceive(DataStreamerPump::event_queue, &item, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (!item) continue;

            // === ROUTING + SINKS ===
            const StreamCompatEntry* compat = stream_get_compat_for_source(item->source_type);
            
            // Example: Input events
            if (item->source_type == SOURCE_INPUT_SYSTEM) {
                const InputEvent* ev = static_cast<const InputEvent*>(item->specific.input_ev_ptr);
                if (ev) {
                    // Keep existing routes
                    if (ev->target == HIDTarget::actAsUsbHID || ...) {
                        RouteInput_HidTarget(*ev);
                    }
                    // Also let focused app react
                    if (auto focused = appManager::instance().get_focused_app()) {
                        focused->on_stream_data(item);
                    }
                }
            }

            // Storage sink example (CBOR + SD)
            if (compat && (compat->can_sink_to & (1 << SINK_STORAGE))) {
                // TODO: encode item->metadata_cbor + payload to CBOR blob
                // write via your d_sdc / FAT
            }

            // Screen / terminal sink, audio, etc.

            dataitem_free(item);   // cleanup
        }

        // Light yield / WDT
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}