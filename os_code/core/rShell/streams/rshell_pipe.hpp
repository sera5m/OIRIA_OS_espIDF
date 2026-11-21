// rshell_pipe.hpp
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

// Forward declarations
class AppBase;
class DataPool;
struct DataItem;

#include "rshell_pool.hpp"
#include "rshell_filter.hpp"

typedef enum Rshell_pipe_flowType {
    null,
    direct,   // 1:1 - one source, one target
    fan,      // 1:N - one source, multiple targets
    merge,    // N:1 - multiple sources, one target
    clone,    // 1:N - same data to all targets (like fan)
    c_fan,    // conditional fan - filtered per target
    c_merge   // conditional merge
} Rshell_pipe_flowType;

// Unified target - can be either an App or a Pool
struct PipeTarget {
    enum class Type { APP, POOL };
    Type type;
    union { AppBase* app; DataPool* pool; };

    static PipeTarget from_app(AppBase* a) { PipeTarget t{Type::APP}; t.app = a; return t; }
    static PipeTarget from_pool(DataPool* p) { PipeTarget t{Type::POOL}; t.pool = p; return t; }
};

// Method: CRUD edits on the payload, plus find / swap / replace / remove.
// That set is closed. No math language and no Toplethos call in the pipe.
// Unit conversion happens at exfil, where the type is already known.
//
// Every subtype holds a PipeFilter (rshell_filter.hpp). Empty filter means
// the bytes pass through unchanged. The subclass still decides the transport.
//
// Base pipe. Subtypes decide how data actually moves.
class RshellPipe {
public:
    enum class Kind {
        InterDevice,   // batched, CRC, for UART / multi-chip
        InterProcess,  // semi-async queue, Unix-like, time not critical
        InterBuffer    // pointer pass-through (transform later)
    };

    virtual ~RshellPipe() = default;

    virtual Kind kind() const = 0;

    std::string id;
    Rshell_pipe_flowType mode = null;
    DataPool* source_pool = nullptr;
    AppBase* source_app = nullptr;
    std::vector<PipeTarget> targets;

    bool add_target(PipeTarget t);
    // Parallel to targets. Used by c_fan so each target can have its own accept filter.
    // Empty filter means "always accept".
    bool add_target_with_filter(PipeTarget t, PipeFilter filter);

    // Shared by every subtype. Empty until someone adds transform steps.
    PipeFilter& filter() { return filter_; }
    const PipeFilter& filter() const { return filter_; }

    // Per-target filter (index matches targets). Empty if none was set.
    const PipeFilter* target_filter(size_t index) const;

    // Push one item. Subtypes implement the actual transfer.
    // Returns false if the item was dropped or the pipe is full.
    virtual bool push(DataItem* item) = 0;

    // Optional flush (used by batched subtypes).
    virtual bool flush() { return true; }

protected:
    PipeFilter filter_;
    std::vector<PipeFilter> target_filters_;

    // Filter into `out`. Empty filter copies the payload. False if a CRUD step fails.
    bool take_bytes(const DataItem* item, std::vector<uint8_t>& out) const;
};

// Inter-device: batched (1-255) with CRC verification on flush.
class InterDevicePipe : public RshellPipe {
public:
    explicit InterDevicePipe(uint8_t batch_count = 1);

    Kind kind() const override { return Kind::InterDevice; }

    bool push(DataItem* item) override;
    bool flush() override;

    uint8_t batch_count() const { return batch_count_; }
    void set_batch_count(uint8_t n);  // clamped to 1..255

    // Last computed CRC of a flushed batch (for the receiver to check).
    uint32_t last_crc() const { return last_crc_; }

private:
    uint8_t batch_count_ = 1;
    // Owned copies. The pump frees the original DataItem after push returns.
    std::vector<std::vector<uint8_t>> batch_;
    uint32_t last_crc_ = 0;

    static uint32_t crc32(const uint8_t* data, size_t len);
};

// Inter-process: semi-async FreeRTOS queue. Time is not critical.
// Unix-style ends (V7/xv6/Linux pipe(7)):
//   read_open / write_open counts. Last writer close → EOF.
//   Last reader close → push returns false (broken pipe).
// Fan/merge stay in the flow graph; this is just the transport.
class InterProcessPipe : public RshellPipe {
public:
    explicit InterProcessPipe(size_t queue_depth = 16);
    ~InterProcessPipe() override;

    Kind kind() const override { return Kind::InterProcess; }

    bool push(DataItem* item) override;

    // Non-blocking pop for the consumer side.
    // Caller owns the returned item and must dataitem_free it.
    // Returns false if empty. Check eof() to distinguish "closed" from "empty".
    bool try_pop(DataItem*& out);

    // Blocking pop. timeout_ms == 0 is try_pop.
    // timeout_ms == portMAX_DELAY waits until data or EOF (no timeout).
    // While blocked, the pipe sets a pause hint so the writer can see
    // that a reader is waiting (and vice versa on a full queue).
    bool pop_wait(DataItem*& out, uint32_t timeout_ms);

    // Pause hints for async systems. True while a side is blocked waiting.
    bool reader_pausing() const { return reader_pausing_; }
    bool writer_pausing() const { return writer_pausing_; }

    // Atomic small-chunk toggle. When on, items with payload <= kAtomicLimit
    // are queued as a single FreeRTOS send (already atomic) and flagged.
    static constexpr size_t kAtomicLimit = 512;
    void set_atomic_small(bool on) { atomic_small_ = on; }
    bool atomic_small() const { return atomic_small_; }

    // Drain up to max items, calling cb for each. Returns number delivered.
    // cb takes ownership and must free the item (or the drain frees on false).
    size_t drain(std::function<bool(DataItem*)> cb, size_t max = 16);

    // Unix ends. Starts with one reader and one writer open.
    void open_read();
    void open_write();
    void close_read();
    void close_write();

    int read_open() const { return read_open_; }
    int write_open() const { return write_open_; }
    bool eof() const;      // no writers left and nothing queued
    bool broken() const;   // no readers left

private:
    void* queue_ = nullptr;  // QueueHandle_t, kept opaque in the header
    size_t depth_ = 16;
    int read_open_ = 1;
    int write_open_ = 1;
    bool reader_pausing_ = false;
    bool writer_pausing_ = false;
    bool atomic_small_ = false;
};

// Inter-buffer: pointer pass-through. A filter, if set, is applied to a
// temporary view for the sink. The view is only valid during push().
class InterBufferPipe : public RshellPipe {
public:
    Kind kind() const override { return Kind::InterBuffer; }

    bool push(DataItem* item) override;

    // Optional direct sink (called with the pointer, no copy).
    void set_sink(std::function<void(DataItem*)> sink) { sink_ = std::move(sink); }

private:
    std::function<void(DataItem*)> sink_;
};
