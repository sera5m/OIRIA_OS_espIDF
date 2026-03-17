#pragma once
#ifndef PSRAM_STD_H
#define PSRAM_STD_H

#include <esp_heap_caps.h>



#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

namespace stdpsram {

// ──────────────────────────────────────────────
// Allocator that requests SPIRAM-capable memory
// ──────────────────────────────────────────────
// psram_std.h (inside namespace stdpsram)

template <typename T>
struct Allocator {
    using value_type      = T;
    using pointer         = T*;
    using const_pointer   = const T*;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;

    // ─── Required for allocator_traits to work correctly ───
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap            = std::true_type;
    using is_always_equal                        = std::true_type;  // since stateless

    Allocator() = default;
    template <class U> constexpr Allocator(const Allocator<U>&) noexcept {}

    pointer allocate(size_type n) {
        if (n == 0) return nullptr;
        pointer p = static_cast<pointer>(heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM));
        if (!p) throw std::bad_alloc();
        return p;
    }

    void deallocate(pointer p, size_type) noexcept {
        heap_caps_free(p);
    }

    // Optional but useful
    pointer reallocate(pointer ptr, size_type new_size) {
        return static_cast<pointer>(heap_caps_realloc(ptr, new_size * sizeof(T), MALLOC_CAP_SPIRAM));
    }

    // Equality (stateless allocators are always equal)
    friend bool operator==(const Allocator&, const Allocator&) noexcept { return true; }
    friend bool operator!=(const Allocator&, const Allocator&) noexcept { return false; }
};

// ──────────────────────────────────────────────
// Convenience aliases
// ──────────────────────────────────────────────

using String = std::basic_string<
    char,
    std::char_traits<char>,
    Allocator<char>
>;

template <typename T>
using Vector = std::vector<T, Allocator<T>>;

// Optional future containers
// template <typename T>
// using Deque = std::deque<T, Allocator<T>>;

// template <typename K, typename V>
// using Map = std::map<K, V, std::less<K>, Allocator<std::pair<const K, V>>>;

} // namespace psram
#endif