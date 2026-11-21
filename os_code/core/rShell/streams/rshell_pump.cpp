#include "os_code/core/rShell/streams/rshell_pool.hpp"
#include "os_code/core/rShell/streams/rshell_pipe.hpp"
#include "os_code/core/rShell/streams/rshell_streamdefs.h"
#include "os_code/core/rShell/streams/rshell_pump.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

static const char* TAG = "DataPump";

namespace {

std::recursive_mutex& pipes_mu() {
    static std::recursive_mutex m;
    return m;
}

std::mutex& life_mu() {
    static std::mutex m;
    return m;
}

std::atomic<bool> g_running{false};
TaskHandle_t g_task = nullptr;

DataItem* clone_item(const DataItem* item) {
    if (!item) return nullptr;
    DataItem* clone = dataitem_new(item->source_type, item->payload_len);
    if (!clone) return nullptr;
    clone->timestamp = item->timestamp;
    clone->source_id = item->source_id;
    clone->compat_flags = item->compat_flags;
    clone->periph_mask = item->periph_mask;
    clone->storage_hint = item->storage_hint;
    if (item->payload && item->payload_len && clone->payload) {
        memcpy(clone->payload, item->payload, item->payload_len);
    }
    if (item->specific.input_ev_ptr && item->payload && clone->payload && item->payload_len > 0) {
        const auto* ev = static_cast<const uint8_t*>(item->specific.input_ev_ptr);
        const uint8_t* base = item->payload;
        if (ev >= base && ev < base + item->payload_len) {
            clone->specific.input_ev_ptr = clone->payload + (ev - base);
        }
    }
    if (item->metadata_cbor && item->metadata_len) {
        clone->metadata_cbor = static_cast<uint8_t*>(malloc(item->metadata_len));
        if (clone->metadata_cbor) {
            memcpy(clone->metadata_cbor, item->metadata_cbor, item->metadata_len);
            clone->metadata_len = item->metadata_len;
        }
    }
    return clone;
}

void deliver_pool(DataPool* pool, const void* src, size_t n) {
    if (!pool || !pool->is_ring() || !src || n == 0) return;
    PoolAccessToken token(pool, DataPool::system_writer(), AccessMode::READ_WRITE);
    if (token.is_valid()) {
        pool->push_ring(src, n);
    }
}

} // namespace

QueueHandle_t DataStreamerPump::event_queue = nullptr;
std::vector<RshellPipe*> DataStreamerPump::active_pipes;
DataStreamerPump gDataStreamer;

std::vector<RshellPipe*> DataStreamerPump::copy_pipes() {
    std::lock_guard<std::recursive_mutex> lock(pipes_mu());
    return active_pipes;
}

void DataStreamerPump::start() {
    std::lock_guard<std::mutex> lock(life_mu());
    if (!event_queue) {
        event_queue = xQueueCreate(64, sizeof(DataItem*));
    }
    if (g_task != nullptr) {
        ESP_LOGW(TAG, "pump already running");
        return;
    }
    g_running.store(true);
    const BaseType_t ok = xTaskCreatePinnedToCore(
        pump_task, "datastream_pump", 8192, nullptr, 2, &g_task, 0);
    if (ok != pdPASS) {
        g_running.store(false);
        g_task = nullptr;
        ESP_LOGE(TAG, "pump task create failed");
        return;
    }
    ESP_LOGI(TAG, "DataStreamer pump started");
}

void DataStreamerPump::stop() {
    g_running.store(false);
    if (event_queue) {
        DataItem* wake = nullptr;
        xQueueSend(event_queue, &wake, 0);
    }
    if (g_task != nullptr && g_task == xTaskGetCurrentTaskHandle()) {
        return;
    }
    for (int i = 0; i < 50 && g_task != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool DataStreamerPump::running() {
    return g_running.load();
}

bool DataStreamerPump::register_pipe(RshellPipe* pipe) {
    if (!pipe) return false;
    std::lock_guard<std::recursive_mutex> lock(pipes_mu());
    if (std::find(active_pipes.begin(), active_pipes.end(), pipe) != active_pipes.end()) {
        return true;
    }
    active_pipes.push_back(pipe);
    ESP_LOGI(TAG, "Registered pipe %s mode=%d", pipe->id.c_str(), (int)pipe->mode);
    return true;
}

bool DataStreamerPump::unregister_pipe(RshellPipe* pipe) {
    if (!pipe) return false;
    std::lock_guard<std::recursive_mutex> lock(pipes_mu());
    auto it = std::find(active_pipes.begin(), active_pipes.end(), pipe);
    if (it == active_pipes.end()) return false;
    active_pipes.erase(it);
    ESP_LOGI(TAG, "Unregistered pipe %s", pipe->id.c_str());
    return true;
}

size_t DataStreamerPump::drain_interprocess_for(AppBase* app, size_t max) {
    if (!app) return 0;
    // Hold the pipe lock so unregister cannot free a pipe mid-drain.
    // Recursive: on_stream_data may register another pipe on this task.
    std::lock_guard<std::recursive_mutex> lock(pipes_mu());
    const std::vector<RshellPipe*> pipes = active_pipes;
    size_t delivered = 0;
    for (auto* pipe : pipes) {
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
    DataItem* item = dataitem_new(SOURCE_INPUT_SYSTEM, sizeof(InputEvent));
    if (!item || !item->payload) {
        dataitem_free(item);
        return false;
    }
    memcpy(item->payload, &ev, sizeof(InputEvent));
    item->specific.input_ev_ptr = item->payload;
    item->timestamp = esp_timer_get_time();
    item->compat_flags = COMPAT_CAN_BRANCH | COMPAT_LOW_LATENCY;
    if (!pushDataItem(item)) {
        dataitem_free(item);
        return false;
    }
    return true;
}

bool DataStreamerPump::pushDataItem(DataItem* item) {
    if (!item || !event_queue) return false;
    return xQueueSend(event_queue, &item, 0) == pdTRUE;
}

void DataStreamerPump::dispatch_item(DataItem* item) {
    if (!item) return;

    if (item->source_type == SOURCE_INPUT_SYSTEM && item->specific.input_ev_ptr) {
        if (auto focused = appManager::instance().get_focused_app()) {
            focused->on_stream_data(item);
        }
    }

    std::lock_guard<std::recursive_mutex> lock(pipes_mu());
    const std::vector<RshellPipe*> pipes = active_pipes;

    for (auto* pipe : pipes) {
        if (!pipe) continue;
        const auto mode = pipe->mode;
        const bool is_fanout = (mode == direct || mode == fan || mode == clone || mode == c_fan);
        const bool is_merge = (mode == merge || mode == c_merge);
        if (!is_fanout && !is_merge) continue;
        if (pipe->kind() == RshellPipe::Kind::InterDevice) continue;

        if (pipe->kind() == RshellPipe::Kind::InterProcess) {
            DataItem* clone = clone_item(item);
            if (clone && !pipe->push(clone)) {
                dataitem_free(clone);
            }
            continue;
        }

        if (is_merge) {
            std::vector<uint8_t> out;
            if (!pipe->filter().empty() &&
                !pipe->filter().apply(item->payload, item->payload_len, out)) {
                continue;
            }
            if (!pipe->targets.empty()) {
                const auto& t = pipe->targets[0];
                if (t.type == PipeTarget::Type::APP && t.app) {
                    t.app->on_stream_data(item);
                } else if (t.type == PipeTarget::Type::POOL && t.pool) {
                    const void* src = out.empty() ? static_cast<const void*>(item->payload) : static_cast<const void*>(out.data());
                    size_t n = out.empty() ? item->payload_len : out.size();
                    deliver_pool(t.pool, src, n);
                }
            }
            continue;
        }

        for (size_t i = 0; i < pipe->targets.size(); ++i) {
            const auto& t = pipe->targets[i];
            const PipeFilter* tf = pipe->target_filter(i);

            std::vector<uint8_t> filtered;
            const bool use_filter = (mode == c_fan) && tf && !tf->empty();
            if (use_filter) {
                if (!tf->apply(item->payload, item->payload_len, filtered)) {
                    continue;
                }
            }

            if (t.type == PipeTarget::Type::APP && t.app) {
                t.app->on_stream_data(item);
            } else if (t.type == PipeTarget::Type::POOL && t.pool) {
                const void* src = filtered.empty() ? static_cast<const void*>(item->payload) : static_cast<const void*>(filtered.data());
                size_t n = filtered.empty() ? item->payload_len : filtered.size();
                deliver_pool(t.pool, src, n);
            }
        }
        if (pipe->kind() == RshellPipe::Kind::InterBuffer) {
            pipe->push(item);
        }
    }
}

void DataStreamerPump::pump_task(void* param) {
    (void)param;
    ESP_LOGI(TAG, "Pump task running");
    DataItem* item = nullptr;

    while (g_running.load()) {
        if (!event_queue) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (xQueueReceive(event_queue, &item, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (!item) continue;
            dispatch_item(item);
            dataitem_free(item);
            item = nullptr;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (event_queue) {
        while (xQueueReceive(event_queue, &item, 0) == pdTRUE) {
            dataitem_free(item);
            item = nullptr;
        }
    }
    g_task = nullptr;
    vTaskDelete(nullptr);
}

extern "C" void datastream_pump_start(void) {
    DataStreamerPump::start();
}

extern "C" bool datastream_push_input(const InputEvent* ev) {
    if (!ev) return false;
    return DataStreamerPump::pushInputEvent(*ev);
}
