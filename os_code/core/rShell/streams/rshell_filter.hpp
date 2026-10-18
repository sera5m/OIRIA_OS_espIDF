#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

// Pipe filter. Closed set. Do not add a math language, a unit conversion,
// or a Toplethos call here. Typed conversion (Fahrenheit to Celsius, and
// the rest of exfil) belongs outside the pipe, at the boundary that
// already knows the type.
//
// The interpreter is not involved. A filter is an ordered list of
// parameterized edits on a working copy of the payload. The original
// bytes stay put until the pipe takes the result. What changes is chosen
// by the arguments, not by a predicate.
//
// Positional:
//   Create(at, bytes)   insert (at == length appends)
//   Read(at, n)         working buffer becomes that slice
//   Update(at, bytes)   overwrite, extending if the write runs past the end
//   Delete(at, n)       erase that span
//
// Same family, selected by pattern instead of by offset alone.
// count 0 means every match. nth is 0-based.
//   Find(pattern, nth)         buffer becomes that match, and nothing else
//   Swap(at_a, at_b, n)        exchange two equal spans; overlap fails
//   Replace(old, neu, count)   non-overlapping, left to right
//   Remove(pattern, count)     drop those spans. One byte selects that byte.
//
// A bad offset, an overlap, or a match that is not there fails the whole
// apply. `out` is left empty on failure.

enum class XformOp : uint8_t {
    Create = 1,
    Read,
    Update,
    Delete,
    Find,
    Swap,
    Replace,
    Remove
};

class PipeFilter {
public:
    bool empty() const { return steps_.empty(); }
    void clear();

    void create(size_t at, const uint8_t* bytes, size_t n);
    void read(size_t at, size_t n);
    void update(size_t at, const uint8_t* bytes, size_t n);
    void erase(size_t at, size_t n);

    void find(const uint8_t* pattern, size_t n, size_t nth = 0);
    void swap_span(size_t at_a, size_t at_b, size_t n);
    void replace(const uint8_t* old_b, size_t old_n,
                 const uint8_t* new_b, size_t new_n,
                 size_t count = 0);
    void remove(const uint8_t* pattern, size_t n, size_t count = 0);

    bool apply(const uint8_t* in, size_t in_len, std::vector<uint8_t>& out) const;

private:
    struct Step {
        XformOp op;
        size_t at;
        size_t n;
        size_t mat;
        size_t at2;
        size_t n2;
        size_t count;
    };

    std::vector<Step> steps_;
    std::vector<uint8_t> material_;

    size_t push_mat(const uint8_t* bytes, size_t n);
};
