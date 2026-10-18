#include "rshell_pipe.hpp"
#include "rshell_streamdefs.h"

#include "esp_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstring>
#include <cstdlib>

static const char* TAG = "RshellPipe";

// ---------------------------------------------------------------------------
// Base
// ---------------------------------------------------------------------------
bool RshellPipe::add_target(PipeTarget t) {
    if (!t.app && !t.pool) return false;
    targets.push_back(t);
    return true;
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

// ---------------------------------------------------------------------------
// InterDevicePipe — batched, CRC on flush
// ---------------------------------------------------------------------------
InterDevicePipe::InterDevicePipe(uint8_t batch_count) {
    set_batch_count(batch_count);
    batch_.reserve(batch_count_);
}

void InterDevicePipe::set_batch_count(uint8_t n) {
    if (n == 0) n = 1;
    batch_count_ = n;  // uint8_t already caps at 255
}

uint32_t InterDevicePipe::crc32(const uint8_t* data, size_t len) {
    // Simple CRC-32 (poly 0xEDB88320). Good enough for UART verification.
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
    batch_.push_back(std::move(bytes));
    if (batch_.size() >= batch_count_) {
        return flush();
    }
    return true;
}

bool InterDevicePipe::flush() {
    if (batch_.empty()) return true;

    uint32_t crc = 0;
    for (const auto& bytes : batch_) {
        if (!bytes.empty()) {
            crc ^= crc32(bytes.data(), bytes.size());
        }
    }
    last_crc_ = crc;

    ESP_LOGI(TAG, "InterDevice flush id=%s count=%u crc=0x%08lx",
             id.c_str(), (unsigned)batch_.size(), (unsigned long)last_crc_);

    // UART send of the batch stays with the collective link. Copies are local.
    batch_.clear();
    return true;
}

// ---------------------------------------------------------------------------
// InterProcessPipe — semi-async FreeRTOS queue
// ---------------------------------------------------------------------------
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
            // caller still owns the item; we do not free it
        }
        vQueueDelete(static_cast<QueueHandle_t>(queue_));
        queue_ = nullptr;
    }
}

bool InterProcessPipe::push(DataItem* item) {
    if (!item || !queue_) return false;
    if (!filter_.empty()) {
        // Queue stores the pointer, so the filtered bytes have to live on the item.
        // The caller still owns the DataItem. A failed send leaves it already rewritten.
        std::vector<uint8_t> out;
        if (!filter_.apply(item->payload, item->payload_len, out)) return false;
        uint8_t* fresh = nullptr;
        if (!out.empty()) {
            fresh = static_cast<uint8_t*>(malloc(out.size()));
            if (!fresh) return false;
            memcpy(fresh, out.data(), out.size());
        }
        free(item->payload);
        item->payload = fresh;
        item->payload_len = out.size();
    }
    // Non-blocking. Semi-async: producer does not wait for the consumer.
    return xQueueSend(static_cast<QueueHandle_t>(queue_), &item, 0) == pdTRUE;
}

bool InterProcessPipe::try_pop(DataItem*& out) {
    if (!queue_) return false;
    DataItem* item = nullptr;
    if (xQueueReceive(static_cast<QueueHandle_t>(queue_), &item, 0) == pdTRUE) {
        out = item;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// InterBufferPipe — pointer pass-through (transform later)
// ---------------------------------------------------------------------------
bool InterBufferPipe::push(DataItem* item) {
    if (!item) return false;
    if (filter_.empty()) {
        if (sink_) sink_(item);
        return true;
    }
    std::vector<uint8_t> out;
    if (!take_bytes(item, out)) return false;
    // Temporary view. The sink must not free it or keep the pointer.
    DataItem view = *item;
    view.payload = out.empty() ? nullptr : out.data();
    view.payload_len = out.size();
    if (sink_) sink_(&view);
    return true;
}
