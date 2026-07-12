#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>
#include <esp_heap_caps.h>

#include "rshell_streamdefs.h"

// Forward declarations instead of full include
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "rshell_streamdefs.h"

class AppBase;

struct MemoryPermission {
    bool read_only = false;
    bool exclusive_write = false;
    bool dma_capable = false;
    bool cacheable = true;
};

enum class AccessMode {
    READ_ONLY,
    READ_WRITE,
    EXCLUSIVE
};

class DataPool;

class PoolAccessToken {
public:
    PoolAccessToken(DataPool* pool, const void* requester, AccessMode mode);
    ~PoolAccessToken();
    PoolAccessToken(PoolAccessToken&& other) noexcept;
    PoolAccessToken& operator=(PoolAccessToken&& other) noexcept;
    PoolAccessToken(const PoolAccessToken&) = delete;
    PoolAccessToken& operator=(const PoolAccessToken&) = delete;

    bool is_valid() const;
    std::byte* data() const;
    size_t size() const;

private:
    DataPool* pool_;
    const void* requester_;
    AccessMode mode_;
    bool acquired_;
};

class DataPool {
public:
    using UniquePoolPtr = std::unique_ptr<DataPool>;
    using SharedPoolPtr = std::shared_ptr<DataPool>;

    static UniquePoolPtr create(size_t bytes,
                                e_type_storage storage_type,
                                const char* owner_name = "unknown",
                                const MemoryPermission& perms = {});

    static SharedPoolPtr create_shared(size_t bytes,
                                       e_type_storage storage_type,
                                       const char* owner_name = "unknown",
                                       const MemoryPermission& perms = {});

    ~DataPool();

    bool acquire(const void* requester, AccessMode mode);
    void release(const void* requester);

    bool has_access(const void* requester) const;
    AccessMode current_access_mode() const;

    std::byte* data();
    const std::byte* data() const;

    size_t size() const;
    e_type_storage storage_type() const;
    const char* owner() const;
    const MemoryPermission& permissions() const;

    bool is_ring() const;
    void set_ring_mode(bool ring);

    bool push_ring(const void* data, size_t size);
    bool pop_ring(void* data, size_t size);
    size_t ring_available() const;

    void zero();
    bool copy_from(const std::byte* src, size_t count, size_t offset = 0);
    bool copy_to(std::byte* dst, size_t count, size_t offset = 0) const;

private:
    DataPool(size_t size, e_type_storage type, const char* owner, const MemoryPermission& perms);

    void* allocate_memory(size_t size, e_type_storage type);
    void free_memory();

    bool validate_access(const void* requester, AccessMode requested_mode) const;

    struct RingBuffer {
        size_t head = 0;
        size_t tail = 0;
        size_t count = 0;
        mutable std::mutex mutex;

        bool push(std::byte* buffer, size_t capacity, const void* data, size_t size);
        bool pop(std::byte* buffer, size_t capacity, void* data, size_t size);
        size_t available() const;
    };

    std::byte* buffer_ = nullptr;
    size_t size_;
    e_type_storage type_;
    MemoryPermission perms_;
    const char* owner_;

    mutable std::mutex access_mutex_;
    std::atomic<uint32_t> reader_count_{0};
    const void* writer_ = nullptr;
    std::atomic<AccessMode> current_mode_{AccessMode::READ_ONLY};

    bool is_ring_ = false;
    std::unique_ptr<RingBuffer> ring_buffer_;
};

namespace psram {

template<typename T, size_t CAPACITY = 1024>
class EventRingBuffer {
public:
    static constexpr size_t CAP = CAPACITY;

    bool push(const T& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t next = (head_ + 1) % CAP;
        if (next == tail_) return false;
        buffer_[head_] = event;
        head_ = next;
        return true;
    }

    bool pop(T& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tail_ == head_) return false;
        event = buffer_[tail_];
        tail_ = (tail_ + 1) % CAP;
        return true;
    }

    size_t available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (head_ >= tail_) return head_ - tail_;
        return CAP - tail_ + head_;
    }

private:
    T buffer_[CAP];
    size_t head_ = 0;
    size_t tail_ = 0;
    mutable std::mutex mutex_;
};

extern EventRingBuffer<int> g_event_ring;

} // namespace psram