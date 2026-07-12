#include "os_code/core/rShell/streams/rshell_pool.hpp"
#include "os_code/core/rShell/streams/rshell_pipe.hpp"
#include "os_code/core/rShell/streams/rshell_streamdefs.h"
#include "os_code/core/rShell/streams/rshell_pump.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hardware/drivers/psram_std/stdpsram.hpp"
#include "d_sdc.h"

#include "os_code/core/rShell/s_hell.hpp"


static const char* TAG = "DataPump";

QueueHandle_t DataStreamerPump::event_queue = nullptr;
std::vector<RshellPipe*> DataStreamerPump::active_pipes;

void DataStreamerPump::start() {
    if (!event_queue) {
        event_queue = xQueueCreate(64, sizeof(DataItem*));
    }

    // PSRAM ring init (global fallback)
    if (!psram::g_ring) {
        psram::g_ring = new (heap_caps_malloc(sizeof(psram::EventRingBuffer), MALLOC_CAP_SPIRAM)) 
                        psram::EventRingBuffer();
    }

    xTaskCreatePinnedToCore(pump_task, "datastream_pump", 8192, nullptr, 2, nullptr, 0);
    ESP_LOGI(TAG, "DataStreamer pump started");
}

bool DataStreamerPump::register_pipe(RshellPipe* pipe) {
    if (!pipe) return false;
    active_pipes.push_back(pipe);
    ESP_LOGI(TAG, "Registered pipe %s mode=%d", pipe->id.c_str(), (int)pipe->mode);
    return true;
}

bool DataStreamerPump::pushInputEvent(const InputEvent& ev) { /* unchanged */ }

bool DataStreamerPump::pushDataItem(DataItem* item) { /* unchanged */ }

static void pump_task(void* param) {
    ESP_LOGI(TAG, "Pump task running");
    DataItem* item = nullptr;

    while (true) {
        if (xQueueReceive(event_queue, &item, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (!item) continue;

            // === BASIC COMPAT ROUTING ===
            const StreamCompatEntry* compat = stream_get_compat(/* name or source-based */);

            // Legacy input handling
            if (item->source_type == SOURCE_INPUT_SYSTEM) {
                const InputEvent* ev = static_cast<const InputEvent*>(item->specific.input_ev_ptr);
                if (ev) {
                    // HID / existing routes...
                    if (auto focused = appManager::instance().get_focused_app()) {
                        focused->on_stream_data(item);
                    }
                }
            }

            // === NEW: PIPE-BASED FAN-OUT / POOL SHARING ===
            
            
            // === PIPE FAN-OUT ===
for (const auto& pipe_ptr : DataStreamerPump::active_pipes) {  // or appManager's
    auto* pipe = pipe_ptr.get();
    if (!pipe) continue;

    bool should_route = (pipe->mode == direct || pipe->mode == fan || pipe->mode == clone);
    if (!should_route) continue;

    for (const auto& t : pipe->targets) {
        if (t.type == PipeTarget::Type::APP && t.app) {
            t.app->on_stream_data(item);
        } else if (t.type == PipeTarget::Type::POOL && t.pool && t.pool->is_ring()) {
            PoolAccessToken token(t.pool, "pump", AccessMode::READ_WRITE);
            if (token.is_valid()) {
                t.pool->push_ring(item->payload, item->payload_len);
            }
        }
    }
}

            // Storage / other sinks
            if (compat && (compat->can_sink_to & (1 << SINK_STORAGE))) {
                // TODO: CBOR serialize + d_sdc write
            }

            dataitem_free(item);
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}