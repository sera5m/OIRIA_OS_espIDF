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
    using value_type = T;
    using pointer    = T*;
    using size_type  = std::size_t;

    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap            = std::true_type;
    using is_always_equal                        = std::true_type;

    Allocator() noexcept = default;
    template <class U> Allocator(const Allocator<U>&) noexcept {}

    pointer allocate(size_type n) {
        if (n == 0) return nullptr;
        pointer p = static_cast<pointer>(
            heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        );
        if (!p) throw std::bad_alloc();
        return p;
    }

    void deallocate(pointer p, size_type) noexcept {
        if (p != nullptr) {                    // ← Critical safety
            heap_caps_free(p);
        }
    }

    // Optional: better reallocate
    pointer reallocate(pointer p, size_type new_n) {
        if (new_n == 0) {
            deallocate(p, 0);
            return nullptr;
        }
        return static_cast<pointer>(
            heap_caps_realloc(p, new_n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        );
    }

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

// Create a String with initial content (avoids double allocation)
inline String make_string(const char* cstr) {
    return String(cstr, Allocator<char>{});
}

inline String make_string(const std::string_view sv) {
    return String(sv.data(), sv.size(), Allocator<char>{});
}

inline String make_string(const char* cstr, size_t len) {
    return String(cstr, len, Allocator<char>{});
}

// Reserve + assign in one go (good for performance)
inline String make_string_reserved(size_t capacity, const char* cstr = nullptr) {
    String s(Allocator<char>{});
    s.reserve(capacity);
    if (cstr) s = cstr;
    return s;
}

// ------------------------------------------------------------------
// Safe numeric conversion helpers (you already have safe_parse_*)
// ------------------------------------------------------------------

// to_string equivalents that go into PSRAM
inline String to_string(int value) {
    char buf[12];  // enough for int32 + sign
    snprintf(buf, sizeof(buf), "%d", value);
    return make_string(buf);
}

inline String to_string(unsigned int value) {
    char buf[11];
    snprintf(buf, sizeof(buf), "%u", value);
    return make_string(buf);
}

inline String to_string(uint16_t value) {   // useful for your colors
    char buf[6];
    snprintf(buf, sizeof(buf), "%u", value);
    return make_string(buf);
}

// Hex version (very useful for colors, debug, etc.)
inline String to_hex_string(uint32_t value, bool uppercase = false, bool prefix = true) {
    char buf[11];  // 0x + 8 hex digits + null
    const char* fmt = prefix ? (uppercase ? "0X%08X" : "0x%08x") : (uppercase ? "%08X" : "%08x");
    snprintf(buf, sizeof(buf), fmt, value);
    return make_string(buf);
}

// ------------------------------------------------------------------
// Debug / statistics helpers (very handy on ESP32)
// ------------------------------------------------------------------

// Get current PSRAM usage (approximate)
inline size_t get_psram_used() {
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    return info.total_allocated_bytes;
}

inline size_t get_psram_free() {
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

inline size_t get_psram_largest_free_block() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}

// Optional: print summary
inline void print_psram_info(const char* tag = "PSRAM") {
    ESP_LOGI(tag, "PSRAM - Used: %zu bytes, Free: %zu bytes, Largest block: %zu bytes",
             get_psram_used(), get_psram_free(), get_psram_largest_free_block());
}



} // namespace psram
#endif