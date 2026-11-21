// rshell_pipe.hpp
#pragma once

#include <atomic>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

class AppBase;
class DataPool;
struct DataItem;

#include "rshell_pool.hpp"
#include "rshell_filter.hpp"

typedef enum Rshell_pipe_flowType {
    null,
    direct,
    fan,
    merge,
    clone,
    c_fan,
    c_merge
} Rshell_pipe_flowType;

struct PipeTarget {
    enum class Type { APP, POOL };
    Type type;
    union { AppBase* app; DataPool* pool; };

    static PipeTarget from_app(AppBase* a) { PipeTarget t{Type::APP}; t.app = a; return t; }
    static PipeTarget from_pool(DataPool* p) { PipeTarget t{Type::POOL}; t.pool = p; return t; }
};

class RshellPipe {
public:
    enum class Kind {
        InterDevice,
        InterProcess,
        InterBuffer
    };

    virtual ~RshellPipe() = default;

    virtual Kind kind() const = 0;

    std::string id;
    Rshell_pipe_flowType mode = null;
    DataPool* source_pool = nullptr;
    AppBase* source_app = nullptr;
    std::vector<PipeTarget> targets;

    bool add_target(PipeTarget t);
    bool add_target_with_filter(PipeTarget t, PipeFilter filter);

    PipeFilter& filter() { return filter_; }
    const PipeFilter& filter() const { return filter_; }

    const PipeFilter* target_filter(size_t index) const;

    virtual bool push(DataItem* item) = 0;
    virtual bool flush() { return true; }

protected:
    PipeFilter filter_;
    std::vector<PipeFilter> target_filters_;

    bool take_bytes(const DataItem* item, std::vector<uint8_t>& out) const;
};

class InterDevicePipe : public RshellPipe {
public:
    explicit InterDevicePipe(uint8_t batch_count = 1);

    Kind kind() const override { return Kind::InterDevice; }

    bool push(DataItem* item) override;
    bool flush() override;

    uint8_t batch_count() const { return batch_count_; }
    void set_batch_count(uint8_t n);

    uint32_t last_crc() const { return last_crc_; }

private:
    uint8_t batch_count_ = 1;
    std::vector<std::vector<uint8_t>> batch_;
    uint32_t last_crc_ = 0;

    static uint32_t crc32(const uint8_t* data, size_t len);
};

class InterProcessPipe : public RshellPipe {
public:
    explicit InterProcessPipe(size_t queue_depth = 16);
    ~InterProcessPipe() override;

    Kind kind() const override { return Kind::InterProcess; }

    bool push(DataItem* item) override;
    bool try_pop(DataItem*& out);
    bool pop_wait(DataItem*& out, uint32_t timeout_ms);

    bool reader_pausing() const { return reader_pausing_.load(); }
    bool writer_pausing() const { return writer_pausing_.load(); }

    static constexpr size_t kAtomicLimit = 512;
    void set_atomic_small(bool on) { atomic_small_.store(on); }
    bool atomic_small() const { return atomic_small_.load(); }

    size_t drain(std::function<bool(DataItem*)> cb, size_t max = 16);

    void open_read();
    void open_write();
    void close_read();
    void close_write();

    int read_open() const { return read_open_.load(); }
    int write_open() const { return write_open_.load(); }
    bool eof() const;
    bool broken() const;

private:
    void* queue_ = nullptr;
    size_t depth_ = 16;
    std::atomic<int> read_open_{1};
    std::atomic<int> write_open_{1};
    std::atomic<bool> reader_pausing_{false};
    std::atomic<bool> writer_pausing_{false};
    std::atomic<bool> atomic_small_{false};
};

class InterBufferPipe : public RshellPipe {
public:
    Kind kind() const override { return Kind::InterBuffer; }

    bool push(DataItem* item) override;

    void set_sink(std::function<void(DataItem*)> sink) { sink_ = std::move(sink); }

private:
    std::function<void(DataItem*)> sink_;
};
