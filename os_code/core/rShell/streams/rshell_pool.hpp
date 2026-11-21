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
#include "hardware/drivers/sd_card/d_sdc.h"
#include "os_code/core/rShell/streams/rshell_nv_pool.hpp"

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

    // Persistent NV Storage on microSD
    bool save_to_rpool(const std::string& filepath);
    bool load_from_rpool(const std::string& filepath);

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
    const void* owner_identity() const;
    // When enforce is true, write/exclusive acquire requires requester == identity.
    // Call with the AppBase* (or other stable identity) after create.
    void set_owner_identity(const void* identity, bool enforce = true);
    bool owner_enforced() const;

    // shm-style attachment. Does not replace shared_ptr ownership;
    // it tracks who currently has the region mapped so a future
    // last-detach can decide to persist or drop.
    void attach();
    void detach();
    int attachments() const;

    bool is_ring() const;
    void set_ring_mode(bool ring);

    // Atomic small-chunk toggle (Unix PIPE_BUF idea). When on, pushes
    // of <= kAtomicLimit bytes are performed under the ring lock as one unit.
    static constexpr size_t kAtomicLimit = 512;
    void set_atomic_small(bool on) { atomic_small_ = on; }
    bool atomic_small() const { return atomic_small_; }

    bool push_ring(const void* data, size_t size);
    bool pop_ring(void* data, size_t size);
    size_t ring_available() const;

    void zero();
    bool copy_from(const std::byte* src, size_t count, size_t offset = 0);
    bool copy_to(std::byte* dst, size_t count, size_t offset = 0) const;


    // Persistent NV Storage (.rpool)
    bool save_to_rpool(const std::string& filepath);
    bool load_from_rpool(const std::string& filepath);

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
    const void* owner_identity_ = nullptr;
    bool enforce_owner_ = false;
    int attach_count_ = 0;

    mutable std::mutex access_mutex_;
    std::atomic<uint32_t> reader_count_{0};
    const void* writer_ = nullptr;
    std::atomic<AccessMode> current_mode_{AccessMode::READ_ONLY};
    bool used_psram_ = false;  // tracks actual allocation method for free()

    bool is_ring_ = false;
    bool atomic_small_ = false;
    std::unique_ptr<RingBuffer> ring_buffer_;
};

// Clean wrapper over a DataPool used as a ring.
// Owns the pool (shared). Can be backed by RAM, PSRAM, or hard storage.
class PoolRing {
public:
    // storage: STORAGE_RAM / STORAGE_PSRAM / STORAGE_MICROSD / STORAGE_NVS.
    // Hard-storage types allocate the working buffer in PSRAM (or RAM fallback)
    // and bind a .rpool path so the ring can be persisted.
    static std::unique_ptr<PoolRing> create(size_t bytes,
                                            e_type_storage storage,
                                            const char* owner_name,
                                            const void* owner_identity = nullptr);

    DataPool* pool() const { return pool_.get(); }
    size_t capacity() const { return pool_ ? pool_->size() : 0; }
    size_t available() const { return pool_ ? pool_->ring_available() : 0; }

    bool push(const void* data, size_t n);
    bool pop(void* data, size_t n);

    // Persist / restore the current buffer contents via .rpool.
    bool save(const std::string& name);
    bool load(const std::string& name);

    void set_atomic_small(bool on);
    bool atomic_small() const;

private:
    explicit PoolRing(DataPool::SharedPoolPtr pool, e_type_storage storage);
    DataPool::SharedPoolPtr pool_;
    e_type_storage storage_;
    std::string persist_name_;
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
