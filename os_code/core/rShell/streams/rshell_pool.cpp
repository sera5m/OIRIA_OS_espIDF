

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
//yes, this is intended to be something akin to how linux has a memory management unit that provides segments in memory for programs
// here we do not have one, and emulate this in software with large ring buffers containing arbitrary data for either ipc or arbitrary blobs
//to avoid memory issues, we have to have pools write from one source via ownership, or more than one at a time via mutex
//read can occur from any program unless read is locked to one, which.... why would i do that outside of userspace protection explicitly
#include "rshell_pool.hpp"
#include "hardware/drivers/sd_card/d_sdc.h"
#include "os_code/core/rShell/streams/rshell_nv_pool.hpp"
//wo we can just get that going for hard storage
//------------------------------------------------------------------------------
// PoolAccessToken Implementation
//------------------------------------------------------------------------------

static const char* TAG = "DataPool";


bool DataPool::save_to_rpool(const std::string& filepath) {
    if (!buffer_ || size_ == 0) {
        ESP_LOGE(TAG, "Cannot save empty pool");
        return false;
    }

    rpool::Header hdr{};
    hdr.sizeBytes = size_;
    strncpy(hdr.ownerAppName, owner_, sizeof(hdr.ownerAppName)-1);
    hdr.ownerPointer = 0;           // Runtime only
    hdr.flags = 0;                  // Set as needed

    std::string fullpath = "/sdcard/" + filepath + ".rpool";

    int fd = open(fullpath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to open %s for write", fullpath.c_str());
        return false;
    }

    // Write header
    write(fd, rpool::FILE_BEGIN, strlen(rpool::FILE_BEGIN));
    write(fd, &hdr, sizeof(hdr));

    // Write pool data
    write(fd, buffer_, size_);

    write(fd, rpool::FILE_END, strlen(rpool::FILE_END));

    close(fd);
    ESP_LOGI(TAG, "Saved pool to %s (size=%zu)", fullpath.c_str(), size_);
    return true;
}

bool DataPool::load_from_rpool(const std::string& filepath) {
    std::string fullpath = "/sdcard/" + filepath + ".rpool";

    int fd = open(fullpath.c_str(), O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to open %s", fullpath.c_str());
        return false;
    }

    char magic[12];
    read(fd, magic, strlen(rpool::FILE_BEGIN));
    if (strncmp(magic, rpool::FILE_BEGIN, strlen(rpool::FILE_BEGIN)) != 0) {
        ESP_LOGE(TAG, "Invalid .rpool magic");
        close(fd);
        return false;
    }

    rpool::Header hdr{};
    read(fd, &hdr, sizeof(hdr));

    // Re-allocate if needed
    if (buffer_) free_memory();
    size_ = hdr.sizeBytes;
    type_ = STORAGE_MICROSD;   // or STORAGE_PSRAM if you want
    buffer_ = static_cast<std::byte*>(allocate_memory(size_, type_));

    if (!buffer_) {
        ESP_LOGE(TAG, "Failed to allocate for load");
        close(fd);
        return false;
    }

    // Read data
    read(fd, buffer_, size_);

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

//------------------------------------------------------------------------------
// DataPool Implementation
//------------------------------------------------------------------------------

DataPool::DataPool(size_t size, e_type_storage type, const char* owner, 
    const MemoryPermission& perms)
: size_(size)
, type_(type)
, perms_(perms)
, owner_(owner)
, ring_buffer_(std::make_unique<RingBuffer>()) {

buffer_ = static_cast<std::byte*>(allocate_memory(size_, type_));
if (!buffer_) {
ESP_LOGE(TAG, "Failed to allocate %zu bytes for pool '%s'", size_, owner_);
throw std::bad_alloc();
}

ESP_LOGI(TAG, "Created pool '%s' of %zu bytes in storage type %d", 
owner_, size_, type_);
}
DataPool::~DataPool() {
    free_memory();
}

void* DataPool::allocate_memory(size_t size, e_type_storage type) {
    void* ptr = nullptr;
    
    switch (type) {
        case STORAGE_RAM:
            ptr = malloc(size);
            break;
            
        case STORAGE_PSRAM:
            ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!ptr) {
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
        switch (type_) {
            case STORAGE_PSRAM:
                heap_caps_free(buffer_);
                break;
            default:
                free(buffer_);
                break;
        }
        buffer_ = nullptr;
    }
}


DataPool::UniquePoolPtr DataPool::create(size_t bytes, e_type_storage storage_type,
    const char* owner_name, 
    const MemoryPermission& perms) {
try {
// Create DataPool - it will allocate memory in its constructor
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

switch (current_mode_) {
case AccessMode::READ_ONLY:
if (reader_count_ > 0) {
reader_count_--;
}
break;

case AccessMode::READ_WRITE:
case AccessMode::EXCLUSIVE:
if (writer_ == requester) {
writer_ = nullptr;
}
break;
}
}

bool DataPool::has_access(const void* requester) const {
std::lock_guard<std::mutex> lock(access_mutex_);

if (requester == writer_) {
return true;
}

if (current_mode_ == AccessMode::READ_ONLY) {
return true;
}

return false;
}

AccessMode DataPool::current_access_mode() const {
return current_mode_;
}

std::byte* DataPool::data() { 
return buffer_; 
}

const std::byte* DataPool::data() const { 
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

bool DataPool::is_ring() const { 
return is_ring_; 
}

void DataPool::set_ring_mode(bool ring) { 
is_ring_ = ring; 
}

bool DataPool::validate_access(const void* requester, AccessMode requested_mode) const {
(void)requester;  // Unused parameter but keep for future validation

if (perms_.read_only && requested_mode != AccessMode::READ_ONLY) {
return false;
}

if (perms_.exclusive_write && requested_mode == AccessMode::READ_WRITE) {
return false;
}

return true;
}

bool DataPool::push_ring(const void* data, size_t size) {
if (!is_ring_ || !ring_buffer_) {
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

//------------------------------------------------------------------------------
// RingBuffer Implementation
//------------------------------------------------------------------------------

bool DataPool::RingBuffer::push(std::byte* buffer, size_t capacity, 
const void* data, size_t size) {
std::lock_guard<std::mutex> lock(mutex);

if (!buffer || !data || size == 0 || size > capacity) {
return false;
}

if (count + size > capacity) {
return false;
}

size_t write_pos = (head + count) % capacity;

if (write_pos + size <= capacity) {
std::memcpy(buffer + write_pos, data, size);
} else {
size_t first_part = capacity - write_pos;
std::memcpy(buffer + write_pos, data, first_part);
std::memcpy(buffer, static_cast<const std::byte*>(data) + first_part, 
size - first_part);
}

count += size;
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
return true;
}

size_t DataPool::RingBuffer::available() const {
std::lock_guard<std::mutex> lock(mutex);
return count;
}

//------------------------------------------------------------------------------
// Global PSRAM Event Ring
//------------------------------------------------------------------------------

namespace psram {
EventRingBuffer<int> g_event_ring;
}
