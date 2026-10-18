#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

// Pipe filter. Not a Toplethos lambda and not a FlowMap.
//
// The interpreter can call a named fn, or copy one slot onto another.
// It cannot build a transform at runtime. A pipe filter is the other
// path: a short list of CRUD ops run in order on a working copy of the
// payload. The original bytes are left alone until the pipe decides
// what to do with the result.
//
//   Create(at, bytes)  insert bytes at `at` (at == length appends)
//   Read(at, n)        the working buffer becomes that slice
//   Update(at, bytes)  overwrite, extending the buffer if the write runs past the end
//   Delete(at, n)      erase the span
//
// An offset past the end, or a span that does not fit, fails the whole
// apply. `out` is left empty on failure.

enum class CrudOp : uint8_t {
    Create = 1,
    Read,
    Update,
    Delete
};

class PipeFilter {
public:
    bool empty() const { return steps_.empty(); }
    void clear();

    void create(size_t at, const uint8_t* bytes, size_t n);
    void read(size_t at, size_t n);
    void update(size_t at, const uint8_t* bytes, size_t n);
    void erase(size_t at, size_t n);

    bool apply(const uint8_t* in, size_t in_len, std::vector<uint8_t>& out) const;

private:
    struct Step {
        CrudOp op;
        size_t at;
        size_t n;
        size_t mat;
    };

    std::vector<Step> steps_;
    std::vector<uint8_t> material_;
};
