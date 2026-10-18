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

// Method: skip the opcode dispatch and call the C++ it would have called.
//
// Honest path:  tph source → opcodes → rsvm_step → C++ native
// Pipe path:    DataItem → the same C++ natives, against a live slot image
//
// The functions are the ones already in the interpreter:
//   fn filter in[payload] out[result]  →  OP_CALL + OP_STORE of the out names
//   FlowMap { src = dst }              →  OP_FLOW (0x91) / rsvm_flow_apply
// out[] is slot names, here, or void. It is not a pointer. The pipe target
// stays in C++ and is filled from the out[] slot after the native returns.
//
// The cheat is limited on purpose. Slots (vm->slots) are mutable and that is
// what a pipe writes: payload in, result out, or one rsvm_flow_apply copy.
// The code array is only written at compile time (the JMP skip patch). There
// is no opcode for rewriting a running function. A pipe must not invent
// bytecode. It calls a function or a FlowMap route that is already loaded
// in the terminal's resident environment, and it does not rsvm_eval per item
// (that wipes the machine).
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

    // Push one item. Subtypes implement the actual transfer.
    // Returns false if the item was dropped or the pipe is full.
    virtual bool push(DataItem* item) = 0;

    // Optional flush (used by batched subtypes).
    virtual bool flush() { return true; }
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
class InterProcessPipe : public RshellPipe {
public:
    explicit InterProcessPipe(size_t queue_depth = 16);
    ~InterProcessPipe() override;

    Kind kind() const override { return Kind::InterProcess; }

    bool push(DataItem* item) override;

    // Non-blocking pop for the consumer side.
    bool try_pop(DataItem*& out);

private:
    void* queue_ = nullptr;  // QueueHandle_t, kept opaque in the header
    size_t depth_ = 16;
};

// Inter-buffer: pure pointer passing. Transform hook left for later.
class InterBufferPipe : public RshellPipe {
public:
    Kind kind() const override { return Kind::InterBuffer; }

    bool push(DataItem* item) override;

    // Optional direct sink (called with the pointer, no copy).
    void set_sink(std::function<void(DataItem*)> sink) { sink_ = std::move(sink); }

private:
    std::function<void(DataItem*)> sink_;
};
