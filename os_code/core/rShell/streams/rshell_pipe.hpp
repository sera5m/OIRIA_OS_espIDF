// rshell_pipe.hpp
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <variant>  // C++17

// Forward declarations
class AppBase;
class DataPool;

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

struct RshellPipe {
    std::string id;
    Rshell_pipe_flowType mode = null;
    DataPool* source_pool = nullptr;
    AppBase* source_app = nullptr;
    std::vector<PipeTarget> targets;

    bool add_target(PipeTarget t);
};

inline bool RshellPipe::add_target(PipeTarget t) {
    if (!t.app && !t.pool) return false;  // simple validity
    targets.push_back(t);
    return true;
}
