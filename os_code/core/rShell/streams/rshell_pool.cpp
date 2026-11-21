
#include "rshell_pool.hpp"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include <cstddef>
#include "os_code/core/rShell/enviroment/env_vars.h"
// Software segments. There is no MMU. Pools are written by one owner,
// or by more than one writer only through the access mutex.
#include "hardware/drivers/sd_card/d_sdc.h"
#include "os_code/core/rShell/streams/rshell_nv_pool.hpp"
#include <cstring>

static const char* TAG = "DataPool";

namespace {

constexpr uint8_t kFlagRing = 0x08;
constexpr uint32_t kRingMagic = 0x52494E47u; // 'RING'

struct RingPersist {
    uint32_t magic;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
};

bool write_full(int fd, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    size_t off = 0;
    while (off < n) {
        ssize_t w = ::write(fd, b + off, n - off);
        if (w <= 0) return false;
        off += static_cast<size_t>(w);
    }
    return true;
}

bool read_full(int fd, void* p, size_t n) {
    uint8_t* b = static_cast<uint8_t*>(p);
    size_t off = 0;
    while (off < n) {
        ssize_t r = ::read(fd, b + off, n - off);
        if (r <= 0) return false;
        off += static_cast<size_t>(r);
    }
    return true;
}

} // namespace

bool DataPool::save_to_rpool(const std::string& filepath) {
    if (!buffer_ || size_ == 0) {
        ESP_LOGE(TAG, "Cannot save empty pool");
        return false;
    }

    rpool::Header hdr{};
    hdr.sizeBytes = size_;
    const char* name = owner_ ? owner_ : "";
    std::strncpy(hdr.ownerAppName, name, sizeof(hdr.ownerAppName) - 1);
    hdr.ownerAppName[sizeof(hdr.ownerAppName) - 1] = 0;
    hdr.ownerPointer = 0;
    hdr.flags = 0;

    RingPersist ring_meta{};
    bool write_ring = false;
    std::unique_lock<std::mutex> ring_lock;
    if (is_ring_ && ring_buffer_) {
        ring_lock = std::unique_lock<std::mutex>(ring_buffer_->mutex);
        ring_meta.magic = kRingMagic;
        ring_meta.head = static_cast<uint32_t>(ring_buffer_->head);
        ring_meta.tail = static_cast<uint32_t>(ring_buffer_->tail);
        ring_meta.count = static_cast<uint32_t>(ring_buffer_->count);
        hdr.flags = static_cast<uint8_t>(hdr.flags | kFlagRing);
        write_ring = true;
    }

    std::string fullpath = "/sdcard/" + filepath + ".rpool";
    int fd = open(fullpath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to open %s for write", fullpath.c_str());
        return false;
    }

    const bool ok =
        write_full(fd, rpool::FILE_BEGIN, sizeof(rpool::FILE_BEGIN) - 1) &&
        write_full(fd, &hdr, sizeof(hdr)) &&
        write_full(fd, buffer_, size_) &&
        (!write_ring || write_full(fd, &ring_meta, sizeof(ring_meta))) &&
        write_full(fd, rpool::FILE_END, sizeof(rpool::FILE_END) - 1);

    if (ok && fsync(fd) != 0) {
        ESP_LOGW(TAG, "fsync failed on %s", fullpath.c_str());
    }
    close(fd);
    if (!ok) {
        unlink(fullpath.c_str());
        ESP_LOGE(TAG, "Short write, removed %s", fullpath.c_str());
        return false;
    }
    ESP_LOGI(TAG, "Saved pool to %s (size=%zu ring=%d)", fullpath.c_str(), size_, write_ring ? 1 : 0);
    return true;
}

bool DataPool::load_from_rpool(const std::string& filepath) {
    std::string fullpath = "/sdcard/" + filepath + ".rpool";

    int fd = open(fullpath.c_str(), O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to open %s", fullpath.c_str());
        return false;
    }

    char magic[sizeof(rpool::FILE_BEGIN)] = {};
    const size_t magic_len = sizeof(rpool::FILE_BEGIN) - 1;
    if (!read_full(fd, magic, magic_len) ||
        std::memcmp(magic, rpool::FILE_BEGIN, magic_len) != 0) {
        ESP_LOGE(TAG, "Invalid .rpool magic");
        close(fd);
        return false;
    }

    rpool::Header hdr{};
    if (!read_full(fd, &hdr, sizeof(hdr))) {
        ESP_LOGE(TAG, "Short read on .rpool header");
        close(fd);
        return false;
    }

    constexpr uint64_t kMaxPoolBytes = 8ull * 1024 * 1024;
    if (hdr.sizeBytes == 0 || hdr.sizeBytes > kMaxPoolBytes) {
        ESP_LOGE(TAG, "Refusing .rpool size %llu", (unsigned long long)hdr.sizeBytes);
        close(fd);
        return false;
    }

    if (buffer_) free_memory();
    const size_t new_size = static_cast<size_t>(hdr.sizeBytes);
    void* fresh = allocate_memory(new_size, STORAGE_PSRAM);
    if (!fresh) {
        size_ = 0;
        ESP_LOGE(TAG, "Failed to allocate for load");
        close(fd);
        return false;
    }
    buffer_ = static_cast<std::byte*>(fresh);
    size_ = new_size;
    type_ = used_psram_ ? STORAGE_PSRAM : STORAGE_RAM;

    if (!read_full(fd, buffer_, size_)) {
        ESP_LOGE(TAG, "Short read of pool payload");
        free_memory();
        size_ = 0;
        close(fd);
        return false;
    }

    if (hdr.flags & kFlagRing) {
        RingPersist ring_meta{};
        if (!read_full(fd, &ring_meta, sizeof(ring_meta)) ||
            ring_meta.magic != kRingMagic ||
            ring_meta.head > size_ || ring_meta.tail > size_ || ring_meta.count > size_) {
            ESP_LOGE(TAG, "Bad ring geometry in .rpool");
            free_memory();
            size_ = 0;
            close(fd);
            return false;
        }
        if (ring_buffer_) {
            std::lock_guard<std::mutex> lock(ring_buffer_->mutex);
            ring_buffer_->head = ring_meta.head;
            ring_buffer_->tail = ring_meta.tail;
            ring_buffer_->count = ring_meta.count;
        }
        is_ring_ = true;
    } else if (ring_buffer_) {
        std::lock_guard<std::mutex> lock(ring_buffer_->mutex);
        ring_buffer_->head = 0;
        ring_buffer_->tail = 0;
        ring_buffer_->count = 0;
    }

    char endmark[sizeof(rpool::FILE_END)] = {};
    const size_t end_len = sizeof(rpool::FILE_END) - 1;
    if (!read_full(fd, endmark, end_len) ||
        std::memcmp(endmark, rpool::FILE_END, end_len) != 0) {
        ESP_LOGE(TAG, "Missing or corrupt .rpool end marker");
        free_memory();
        size_ = 0;
        close(fd);
        return false;
    }

    close(fd);
    ESP_LOGI(TAG, "Loaded pool from %s (size=%zu)", fullpath.c_str(), size_);
    return true;
}

PoolAccessToken::PoolAccessToken(DataPool* pool, const void* requester, AccessMode mode)
    : pool_(pool), requester_(requester), mode_(mode), acquired_(false) {
    if (pool_) {
        acquired_ = pool_->acquire(requester, mode);
    }
}

PoolAccessToken::~PoolAccessToken() {
    if (acquired_ && pool_) {
        pool_->release(requester_);
    }
}

PoolAccessToken::PoolAccessToken(PoolAccessToken&& other) noexcept
    : pool_(other.pool_), requester_(other.requester_),
      mode_(other.mode_), acquired_(other.acquired_) {
    other.acquired_ = false;
    other.pool_ = nullptr;
}

PoolAccessToken& PoolAccessToken::operator=(PoolAccessToken&& other) noexcept {
    if (this != &other) {
        if (acquired_ && pool_) {
            pool_->release(requester_);
        }
        pool_ = other.pool_;
        requester_ = other.requester_;
        mode_ = other.mode_;
        acquired_ = other.acquired_;
        other.acquired_ = false;
        other.pool_ = nullptr;
    }
    return *this;
}

bool PoolAccessToken::is_valid() const {
    return acquired_;
}

std::byte* PoolAccessToken::data() const {
    return (acquired_ && pool_) ? pool_->data() : nullptr;
}

size_t PoolAccessToken::size() const {
    return (acquired_ && pool_) ? pool_->size() : 0;
}

bool PoolAccessToken::save_to_rpool(const std::string& filepath) {
    if (!acquired_ || !pool_) return false;
    return pool_->save_to_rpool(filepath);
}

bool PoolAccessToken::load_from_rpool(const std::string& filepath) {
    if (!acquired_ || !pool_) return false;
    return pool_->load_from_rpool(filepath);
}

DataPool::DataPool(size_t size, e_type_storage type, const char* owner,
    const MemoryPermission& perms)
: size_(size)
, type_(type)
, perms_(perms)
, owner_(owner)
, ring_buffer_(std::make_unique<RingBuffer>()) {

buffer_ = static_cast<std::byte*>(allocate_memory(size_, type_));
if (!buffer_) {
ESP_LOGE(TAG, "Failed to allocate %zu bytes for pool '%s'", size_, owner_ ? owner_ : "?");
throw std::bad_alloc();
}

ESP_LOGI(TAG, "Created pool '%s' of %zu bytes in storage type %d",
owner_ ? owner_ : "?", size_, type_);
}

DataPool::~DataPool() {
    free_memory();
}

void* DataPool::allocate_memory(size_t size, e_type_storage type) {
    void* ptr = nullptr;
    used_psram_ = false;

    switch (type) {
        case STORAGE_RAM:
            ptr = malloc(size);
            break;

        case STORAGE_PSRAM:
            ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (ptr) {
                used_psram_ = true;
            } else {
                ESP_LOGW(TAG, "PSRAM allocation failed, falling back to internal RAM");
                ptr = malloc(size);
            }
            break;

        case STORAGE_NVS:
        case STORAGE_MICROSD:
        case STORAGE_EXT_NONVOL:
        case STORAGE_EXT_VOL:
        case STORAGE_SEND_EXTDEV:
            ESP_LOGW(TAG, "Storage type %d not yet supported, falling back to RAM", type);
            ptr = malloc(size);
            break;

        default:
            ptr = malloc(size);
            break;
    }

    return ptr;
}

void DataPool::free_memory() {
    if (buffer_) {
        if (used_psram_) {
            heap_caps_free(buffer_);
        } else {
            free(buffer_);
        }
        buffer_ = nullptr;
        used_psram_ = false;
    }
}

DataPool::UniquePoolPtr DataPool::create(size_t bytes, e_type_storage storage_type,
    const char* owner_name,
    const MemoryPermission& perms) {
try {
return UniquePoolPtr(new DataPool(bytes, storage_type, owner_name, perms));
} catch (const std::bad_alloc& e) {
ESP_LOGE(TAG, "Failed to create DataPool '%s': %s", owner_name, e.what());
return nullptr;
}
}

DataPool::SharedPoolPtr DataPool::create_shared(size_t bytes, e_type_storage storage_type,
    const char* owner_name,
    const MemoryPermission& perms) {
try {
return SharedPoolPtr(new DataPool(bytes, storage_type, owner_name, perms));
} catch (const std::bad_alloc& e) {
ESP_LOGE(TAG, "Failed to create shared DataPool '%s': %s", owner_name, e.what());
return nullptr;
}
}

bool DataPool::acquire(const void* requester, AccessMode mode) {
    std::lock_guard<std::mutex> lock(access_mutex_);

    if (!validate_access(requester, mode)) {
        return false;
    }

    switch (mode) {
    case AccessMode::READ_ONLY:
        if (writer_ != nullptr) {
            return false;
        }
        reader_count_++;
        break;

    case AccessMode::READ_WRITE:
        if (writer_ != nullptr && writer_ != requester) {
            return false;
        }
        if (reader_count_ > 0) {
            return false;
        }
        writer_ = requester;
        break;

    case AccessMode::EXCLUSIVE:
        if (writer_ != nullptr || reader_count_ > 0) {
            return false;
        }
        writer_ = requester;
        break;
    }

    current_mode_ = mode;
    return true;
}

void DataPool::release(const void* requester) {
    std::lock_guard<std::mutex> lock(access_mutex_);

    if (writer_ == requester) {
        writer_ = nullptr;
        return;
    }

    if (reader_count_ > 0) {
        reader_count_--;
    }
}

bool DataPool::has_access(const void* requester) const {
    std::lock_guard<std::mutex> lock(access_mutex_);

    if (requester == writer_) {
        return true;
    }

    if (writer_ == nullptr && reader_count_ > 0) {
        return true;
    }

    return false;
}

AccessMode DataPool::current_access_mode() const {
    return current_mode_;
}

std::byte* DataPool::data() {
    std::lock_guard<std::mutex> lock(access_mutex_);
    if (writer_ == nullptr && reader_count_ == 0) {
        ESP_LOGW(TAG, "data() called with no active acquisition on pool '%s'", owner_ ? owner_ : "?");
    }
    return buffer_;
}

const std::byte* DataPool::data() const {
    std::lock_guard<std::mutex> lock(access_mutex_);
    if (writer_ == nullptr && reader_count_ == 0) {
        ESP_LOGW(TAG, "data() called with no active acquisition on pool '%s'", owner_ ? owner_ : "?");
    }
    return buffer_;
}

size_t DataPool::size() const {
    return size_;
}

e_type_storage DataPool::storage_type() const {
    return type_;
}

const char* DataPool::owner() const {
    return owner_;
}

const MemoryPermission& DataPool::permissions() const {
    return perms_;
}

void DataPool::attach() {
    std::lock_guard<std::mutex> lock(access_mutex_);
    ++attach_count_;
}

void DataPool::detach() {
    std::lock_guard<std::mutex> lock(access_mutex_);
    if (attach_count_ > 0) --attach_count_;
}

int DataPool::attachments() const {
    std::lock_guard<std::mutex> lock(access_mutex_);
    return attach_count_;
}

bool DataPool::is_ring() const {
    return is_ring_;
}

void DataPool::set_ring_mode(bool ring) {
    is_ring_ = ring;
}

bool DataPool::validate_access(const void* requester, AccessMode requested_mode) const {
    if (perms_.read_only && requested_mode != AccessMode::READ_ONLY) {
        return false;
    }

    if (perms_.exclusive_write && requested_mode == AccessMode::READ_WRITE) {
        return false;
    }

    // Writes must come from the owner, or from the pump's system identity.
    // Reads stay open. This is software isolation, not a sandbox.
    if (enforce_owner_ && owner_identity_ != nullptr &&
        requested_mode != AccessMode::READ_ONLY &&
        requester != owner_identity_ &&
        requester != system_writer()) {
        return false;
    }

    return true;
}

const void* DataPool::owner_identity() const {
    return owner_identity_;
}

const void* DataPool::system_writer() {
    static const char tag = 'S';
    return &tag;
}

void DataPool::set_owner_identity(const void* identity, bool enforce) {
    std::lock_guard<std::mutex> lock(access_mutex_);
    owner_identity_ = identity;
    enforce_owner_ = enforce && identity != nullptr;
}

bool DataPool::owner_enforced() const {
    return enforce_owner_;
}

bool DataPool::push_ring(const void* data, size_t size) {
    if (!is_ring_ || !ring_buffer_) {
        return false;
    }
    if (atomic_small_ && size > kAtomicLimit) {
        return false;
    }
    return ring_buffer_->push(buffer_, size_, data, size);
}

bool DataPool::pop_ring(void* data, size_t size) {
    if (!is_ring_ || !ring_buffer_) {
        return false;
    }
    return ring_buffer_->pop(buffer_, size_, data, size);
}

size_t DataPool::ring_available() const {
    if (!is_ring_ || !ring_buffer_) {
        return 0;
    }
    return ring_buffer_->available();
}

void DataPool::zero() {
    if (buffer_) {
        std::memset(buffer_, 0, size_);
    }
}

bool DataPool::copy_from(const std::byte* src, size_t count, size_t offset) {
    if (!buffer_ || !src || offset + count > size_) {
        return false;
    }
    std::memcpy(buffer_ + offset, src, count);
    return true;
}

bool DataPool::copy_to(std::byte* dst, size_t count, size_t offset) const {
    if (!buffer_ || !dst || offset + count > size_) {
        return false;
    }
    std::memcpy(dst, buffer_ + offset, count);
    return true;
}

bool DataPool::RingBuffer::push(std::byte* buffer, size_t capacity,
                                const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!buffer || !data || size == 0 || size > capacity) {
        return false;
    }

    if (count + size > capacity) {
        return false;
    }

    // head is the read index and is kept equal to tail. The old write
    // index (head + count) never advanced head, so a pop then a push
    // overwrote bytes that were still live.
    size_t write_pos = (tail + count) % capacity;

    if (write_pos + size <= capacity) {
        std::memcpy(buffer + write_pos, data, size);
    } else {
        size_t first_part = capacity - write_pos;
        std::memcpy(buffer + write_pos, data, first_part);
        std::memcpy(buffer, static_cast<const std::byte*>(data) + first_part,
                    size - first_part);
    }

    count += size;
    head = tail;
    return true;
}

bool DataPool::RingBuffer::pop(std::byte* buffer, size_t capacity,
                               void* data, size_t size) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!buffer || !data || size == 0 || size > count) {
        return false;
    }

    if (tail + size <= capacity) {
        std::memcpy(data, buffer + tail, size);
    } else {
        size_t first_part = capacity - tail;
        std::memcpy(data, buffer + tail, first_part);
        std::memcpy(static_cast<std::byte*>(data) + first_part, buffer,
                    size - first_part);
    }

    tail = (tail + size) % capacity;
    count -= size;
    head = tail;
    return true;
}

size_t DataPool::RingBuffer::available() const {
    std::lock_guard<std::mutex> lock(mutex);
    return count;
}

namespace psram {
EventRingBuffer<int> g_event_ring;
}

PoolRing::PoolRing(DataPool::SharedPoolPtr pool, e_type_storage storage)
    : pool_(std::move(pool)), storage_(storage) {}

std::unique_ptr<PoolRing> PoolRing::create(size_t bytes,
                                           e_type_storage storage,
                                           const char* owner_name,
                                           const void* owner_identity) {
    e_type_storage working = storage;
    if (storage == STORAGE_MICROSD || storage == STORAGE_NVS ||
        storage == STORAGE_EXT_NONVOL) {
        working = STORAGE_PSRAM;
    }
    auto pool = DataPool::create_shared(bytes, working, owner_name);
    if (!pool) return nullptr;
    pool->set_ring_mode(true);
    if (owner_identity) pool->set_owner_identity(owner_identity, true);
    return std::unique_ptr<PoolRing>(new PoolRing(std::move(pool), storage));
}

bool PoolRing::push(const void* data, size_t n) {
    return pool_ && pool_->push_ring(data, n);
}

bool PoolRing::pop(void* data, size_t n) {
    return pool_ && pool_->pop_ring(data, n);
}

bool PoolRing::save(const std::string& name) {
    if (!pool_) return false;
    persist_name_ = name;
    return pool_->save_to_rpool(name);
}

bool PoolRing::load(const std::string& name) {
    if (!pool_) return false;
    persist_name_ = name;
    bool ok = pool_->load_from_rpool(name);
    if (ok) pool_->set_ring_mode(true);
    return ok;
}

void PoolRing::set_atomic_small(bool on) {
    if (pool_) pool_->set_atomic_small(on);
}

bool PoolRing::atomic_small() const {
    return pool_ && pool_->atomic_small();
}
