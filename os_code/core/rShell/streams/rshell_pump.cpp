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
#include <algorithm>
#include <cstring>

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

bool DataStreamerPump::unregister_pipe(RshellPipe* pipe) {
    if (!pipe) return false;
    auto it = std::find(active_pipes.begin(), active_pipes.end(), pipe);
    if (it == active_pipes.end()) return false;
    active_pipes.erase(it);
    ESP_LOGI(TAG, "Unregistered pipe %s", pipe->id.c_str());
    return true;
}

size_t DataStreamerPump::drain_interprocess_for(AppBase* app, size_t max) {
    if (!app) return 0;
    size_t delivered = 0;
    for (auto* pipe : active_pipes) {
        if (!pipe || pipe->kind() != RshellPipe::Kind::InterProcess) continue;
        auto* ip = static_cast<InterProcessPipe*>(pipe);
        bool targets_app = false;
        for (const auto& t : pipe->targets) {
            if (t.type == PipeTarget::Type::APP && t.app == app) {
                targets_app = true;
                break;
            }
        }
        if (!targets_app && pipe->source_app != app) continue;

        delivered += ip->drain([&](DataItem* item) {
            app->on_stream_data(item);
            dataitem_free(item);
            return true;
        }, max);
    }
    return delivered;
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

            // Dispatch by flow mode.
            // InterDevice is caller-driven (push/flush).
            // InterProcess is queued; consumers try_pop/drain.
            // direct/fan/clone/c_fan/c_merge/merge are handled here.
            for (auto* pipe : DataStreamerPump::active_pipes) {
                if (!pipe) continue;
                const auto mode = pipe->mode;
                const bool is_fanout = (mode == direct || mode == fan || mode == clone || mode == c_fan);
                const bool is_merge = (mode == merge || mode == c_merge);
                if (!is_fanout && !is_merge) continue;
                if (pipe->kind() == RshellPipe::Kind::InterDevice) continue;

                if (pipe->kind() == RshellPipe::Kind::InterProcess) {
                    // Queue a clone. Consumer owns it and must free after try_pop/drain.
                    DataItem* clone = dataitem_new(item->source_type, item->payload_len);
                    if (clone) {
                        clone->timestamp = item->timestamp;
                        clone->source_id = item->source_id;
                        clone->compat_flags = item->compat_flags;
                        clone->periph_mask = item->periph_mask;
                        clone->storage_hint = item->storage_hint;
                        if (item->payload && item->payload_len && clone->payload) {
                            memcpy(clone->payload, item->payload, item->payload_len);
                        }
                        if (!pipe->push(clone)) {
                            dataitem_free(clone);
                        }
                    }
                    continue;
                }

                // Conditional merge / merge: pipe filter is the gate. Single target.
                if (is_merge) {
                    std::vector<uint8_t> out;
                    if (!pipe->filter().empty() &&
                        !pipe->filter().apply(item->payload, item->payload_len, out)) {
                        continue;  // condition failed
                    }
                    if (!pipe->targets.empty()) {
                        const auto& t = pipe->targets[0];
                        if (t.type == PipeTarget::Type::APP && t.app) {
                            t.app->on_stream_data(item);
                        } else if (t.type == PipeTarget::Type::POOL && t.pool && t.pool->is_ring()) {
                            PoolAccessToken token(t.pool, "pump", AccessMode::READ_WRITE);
                            if (token.is_valid()) {
                                const void* src = out.empty() ? item->payload : out.data();
                                size_t n = out.empty() ? item->payload_len : out.size();
                                t.pool->push_ring(src, n);
                            }
                        }
                    }
                    continue;
                }

                // Fan-out (direct/fan/clone) and conditional fan.
                for (size_t i = 0; i < pipe->targets.size(); ++i) {
                    const auto& t = pipe->targets[i];
                    const PipeFilter* tf = pipe->target_filter(i);

                    std::vector<uint8_t> filtered;
                    const bool use_filter = (mode == c_fan) && tf && !tf->empty();
                    if (use_filter) {
                        if (!tf->apply(item->payload, item->payload_len, filtered)) {
                            continue;  // this target rejects the item
                        }
                    }

                    if (t.type == PipeTarget::Type::APP && t.app) {
                        t.app->on_stream_data(item);
                    } else if (t.type == PipeTarget::Type::POOL && t.pool && t.pool->is_ring()) {
                        PoolAccessToken token(t.pool, "pump", AccessMode::READ_WRITE);
                        if (token.is_valid()) {
                            const void* src = filtered.empty() ? item->payload : filtered.data();
                            size_t n = filtered.empty() ? item->payload_len : filtered.size();
                            t.pool->push_ring(src, n);
                        }
                    }
                }
                if (pipe->kind() == RshellPipe::Kind::InterBuffer) {
                    pipe->push(item);
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
