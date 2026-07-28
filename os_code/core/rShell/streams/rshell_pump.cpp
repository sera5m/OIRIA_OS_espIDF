#include "os_code/core/rShell/streams/rshell_pool.hpp"
#include "os_code/core/rShell/streams/rshell_pipe.hpp"
#include "os_code/core/rShell/streams/rshell_streamdefs.h"
#include "os_code/core/rShell/streams/rshell_pump.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "hardware/drivers/psram_std/psram_std.hpp"
#include "os_code/core/window_env/wenv_basicThemes.h"
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"

// Remove duplicate includes of esp_log.h and esp_timer.h



static const char* TAG = "DataPump";

QueueHandle_t DataStreamerPump::event_queue = nullptr;
std::vector<RshellPipe*> DataStreamerPump::active_pipes;

void DataStreamerPump::start() {
    if (!event_queue) {
        event_queue = xQueueCreate(64, sizeof(DataItem*));
    }

    // PSRAM ring is handled in DataPool - no need for g_ring here
    xTaskCreatePinnedToCore(pump_task, "datastream_pump", 8192, nullptr, 2, nullptr, 0);
    ESP_LOGI(TAG, "DataStreamer pump started");
}


bool DataStreamerPump::register_pipe(RshellPipe* pipe) {
    if (!pipe) return false;
    active_pipes.push_back(pipe);
    ESP_LOGI(TAG, "Registered pipe %s mode=%d", pipe->id.c_str(), (int)pipe->mode);
    return true;
}

bool DataStreamerPump::pushInputEvent(const InputEvent& ev) {
    DataItem* item = dataitem_new(SOURCE_INPUT_SYSTEM, 0);
    if (!item) return false;

    item->specific.input_ev_ptr = (void*)&ev;
    item->timestamp = esp_timer_get_time();
    item->compat_flags = COMPAT_CAN_BRANCH | COMPAT_LOW_LATENCY;

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

            // Legacy input handling
            if (item->source_type == SOURCE_INPUT_SYSTEM) {
                const InputEvent* ev = static_cast<const InputEvent*>(item->specific.input_ev_ptr);
                if (ev) {
                    if (auto focused = appManager::instance().get_focused_app()) {
                        focused->on_stream_data(item);
                    }
                }
            }

            // PIPE FAN-OUT
            for (auto* pipe : DataStreamerPump::active_pipes) {
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

            dataitem_free(item);
            //sneed harder, we have
            // Storage / other sinks
           // if (compat && (compat->can_sink_to & (1 << SINK_STORAGE))) {
                // TODO: CBOR serialize + d_sdc write
            //}

            //dataitem_free(item);
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
