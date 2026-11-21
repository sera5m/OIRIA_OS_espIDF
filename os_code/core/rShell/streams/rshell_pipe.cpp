#include "rshell_pipe.hpp"
#include "rshell_streamdefs.h"

#include "esp_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstring>
#include <cstdlib>
#include "os_code/core/com/rs_collective_uart.h"

static const char* TAG = "RshellPipe";

namespace {

int clamp_dec(std::atomic<int>& counter) {
    int prev = counter.fetch_sub(1);
    if (prev <= 0) {
        counter.fetch_add(1);
        return 0;
    }
    return prev - 1;
}

bool uart_emit(const void* p, size_t n) {
    if (n == 0) return true;
    const int w = rs_coll_uart_write(p, n);
    return w >= 0 && static_cast<size_t>(w) == n;
}

} // namespace

bool RshellPipe::add_target(PipeTarget t) {
    if (!t.app && !t.pool) return false;
    targets.push_back(t);
    target_filters_.emplace_back();
    return true;
}

bool RshellPipe::add_target_with_filter(PipeTarget t, PipeFilter filter) {
    if (!add_target(t)) return false;
    target_filters_.back() = std::move(filter);
    return true;
}

const PipeFilter* RshellPipe::target_filter(size_t index) const {
    if (index >= target_filters_.size()) return nullptr;
    return &target_filters_[index];
}

bool RshellPipe::take_bytes(const DataItem* item, std::vector<uint8_t>& out) const {
    if (!item) return false;
    const uint8_t* src = item->payload;
    size_t n = item->payload_len;
    if (!src) n = 0;
    if (filter_.empty()) {
        out.assign(src, src + n);
        return true;
    }
    return filter_.apply(src, n, out);
}

InterDevicePipe::InterDevicePipe(uint8_t batch_count) {
    set_batch_count(batch_count);
    batch_.reserve(batch_count_);
}

void InterDevicePipe::set_batch_count(uint8_t n) {
    if (n == 0) n = 1;
    batch_count_ = n;
}

uint32_t InterDevicePipe::crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

bool InterDevicePipe::push(DataItem* item) {
    if (!item) return false;
    std::vector<uint8_t> bytes;
    if (!take_bytes(item, bytes)) return false;
    if (bytes.size() > 0xFFFF) {
        ESP_LOGE(TAG, "InterDevice %s: payload %u does not fit in uint16",
                 id.c_str(), (unsigned)bytes.size());
        return false;
    }
    batch_.push_back(std::move(bytes));
    if (batch_.size() >= batch_count_) {
        return flush();
    }
    return true;
}

bool InterDevicePipe::flush() {
    if (batch_.empty()) return true;

    for (const auto& bytes : batch_) {
        if (bytes.size() > 0xFFFF) {
            ESP_LOGE(TAG, "InterDevice %s: oversized payload, batch dropped", id.c_str());
            batch_.clear();
            return false;
        }
    }

    uint32_t crc = 0;
    for (const auto& bytes : batch_) {
        if (!bytes.empty()) {
            crc ^= crc32(bytes.data(), bytes.size());
        }
    }
    last_crc_ = crc;

    if (!rs_coll_uart_ready()) {
        ESP_LOGW(TAG, "InterDevice flush: collective UART not ready, batch dropped");
        batch_.clear();
        return false;
    }

    bool ok = true;
    uint8_t count = static_cast<uint8_t>(batch_.size());
    ok = ok && uart_emit(&count, 1);
    for (const auto& bytes : batch_) {
        uint16_t len = static_cast<uint16_t>(bytes.size());
        ok = ok && uart_emit(&len, sizeof(len));
        if (!bytes.empty()) ok = ok && uart_emit(bytes.data(), bytes.size());
    }
    ok = ok && uart_emit(&last_crc_, sizeof(last_crc_));

    batch_.clear();
    if (!ok) {
        ESP_LOGE(TAG, "InterDevice flush id=%s short UART write", id.c_str());
        return false;
    }
    ESP_LOGI(TAG, "InterDevice flush id=%s count=%u crc=0x%08lx",
             id.c_str(), (unsigned)count, (unsigned long)last_crc_);
    return true;
}

InterProcessPipe::InterProcessPipe(size_t queue_depth)
    : depth_(queue_depth ? queue_depth : 16) {
    queue_ = xQueueCreate(static_cast<UBaseType_t>(depth_), sizeof(DataItem*));
    if (!queue_) {
        ESP_LOGE(TAG, "InterProcessPipe: queue create failed");
    }
}

InterProcessPipe::~InterProcessPipe() {
    if (queue_) {
        DataItem* leftover = nullptr;
        while (xQueueReceive(static_cast<QueueHandle_t>(queue_), &leftover, 0) == pdTRUE) {
            // push() transferred ownership. A null slot is the EOF sentinel.
            dataitem_free(leftover);
        }
        vQueueDelete(static_cast<QueueHandle_t>(queue_));
        queue_ = nullptr;
    }
}

bool InterProcessPipe::push(DataItem* item) {
    if (!item || !queue_) return false;
    if (read_open_.load() <= 0) {
        ESP_LOGW(TAG, "InterProcessPipe %s: broken (no readers)", id.c_str());
        return false;
    }

    if (!filter_.empty()) {
        std::vector<uint8_t> out;
        if (!filter_.apply(item->payload, item->payload_len, out)) return false;
        if (atomic_small_.load() && out.size() > kAtomicLimit) {
            ESP_LOGW(TAG, "InterProcessPipe %s: atomic_small refuses %u bytes",
                     id.c_str(), (unsigned)out.size());
            return false;
        }
        uint8_t* fresh = nullptr;
        if (!out.empty()) {
            fresh = static_cast<uint8_t*>(malloc(out.size()));
            if (!fresh) return false;
            memcpy(fresh, out.data(), out.size());
        }
        free(item->payload);
        item->payload = fresh;
        item->payload_len = out.size();
    } else if (atomic_small_.load() && item->payload_len > kAtomicLimit) {
        ESP_LOGW(TAG, "InterProcessPipe %s: atomic_small refuses %u bytes",
                 id.c_str(), (unsigned)item->payload_len);
        return false;
    }

    if (uxQueueSpacesAvailable(static_cast<QueueHandle_t>(queue_)) == 0) {
        writer_pausing_.store(true);
        ESP_LOGD(TAG, "InterProcessPipe %s: writer pausing (queue full)", id.c_str());
    }
    const BaseType_t ok = xQueueSend(static_cast<QueueHandle_t>(queue_), &item, 0);
    if (ok == pdTRUE) writer_pausing_.store(false);
    return ok == pdTRUE;
}

bool InterProcessPipe::try_pop(DataItem*& out) {
    return pop_wait(out, 0);
}

bool InterProcessPipe::pop_wait(DataItem*& out, uint32_t timeout_ms) {
    if (!queue_) return false;
    DataItem* item = nullptr;
    TickType_t ticks = 0;
    if (timeout_ms == 0) {
        ticks = 0;
    } else if (timeout_ms == portMAX_DELAY) {
        ticks = portMAX_DELAY;
        reader_pausing_.store(true);
        ESP_LOGD(TAG, "InterProcessPipe %s: reader pausing (waiting)", id.c_str());
    } else {
        ticks = pdMS_TO_TICKS(timeout_ms);
        reader_pausing_.store(true);
    }
    const BaseType_t ok = xQueueReceive(static_cast<QueueHandle_t>(queue_), &item, ticks);
    reader_pausing_.store(false);
    if (ok == pdTRUE) {
        if (item == nullptr) {
            out = nullptr;
            return false;
        }
        out = item;
        return true;
    }
    out = nullptr;
    return false;
}

size_t InterProcessPipe::drain(std::function<bool(DataItem*)> cb, size_t max) {
    size_t n = 0;
    while (n < max) {
        DataItem* item = nullptr;
        if (!try_pop(item)) break;
        if (!cb || !cb(item)) {
            dataitem_free(item);
        }
        ++n;
    }
    return n;
}

void InterProcessPipe::open_read()  { read_open_.fetch_add(1); }
void InterProcessPipe::open_write() { write_open_.fetch_add(1); }

void InterProcessPipe::close_read() {
    clamp_dec(read_open_);
}

void InterProcessPipe::close_write() {
    const int left = clamp_dec(write_open_);
    if (left != 0 || !queue_ || read_open_.load() <= 0) return;
    int n = read_open_.load();
    if (n > 8) n = 8;
    for (int i = 0; i < n; ++i) {
        DataItem* eof_tok = nullptr;
        if (xQueueSend(static_cast<QueueHandle_t>(queue_), &eof_tok, 0) != pdTRUE) break;
    }
    ESP_LOGD(TAG, "InterProcessPipe %s: writers closed, readers woken", id.c_str());
}

bool InterProcessPipe::eof() const {
    if (write_open_.load() > 0) return false;
    if (!queue_) return true;
    return uxQueueMessagesWaiting(static_cast<QueueHandle_t>(queue_)) == 0;
}

bool InterProcessPipe::broken() const {
    return read_open_.load() <= 0;
}

bool InterBufferPipe::push(DataItem* item) {
    if (!item) return false;
    if (filter_.empty()) {
        if (sink_) sink_(item);
        return true;
    }
    std::vector<uint8_t> out;
    if (!take_bytes(item, out)) return false;
    DataItem view = *item;
    view.payload = out.empty() ? nullptr : out.data();
    view.payload_len = out.size();
    if (sink_) sink_(&view);
    return true;
}
