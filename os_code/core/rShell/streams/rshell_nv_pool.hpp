#pragma once

#include <cstdint>
#include <string>


namespace rpool{
    // On-disk magic is UTF-8 U+1F5CE (bytes F0 9F 97 8E) three times.
    // FILE_BEGIN is 18 bytes, FILE_END is 16. Explicit casts so the bytes
    // cannot be mangled and so a signed char does not overflow the literal.
    static constexpr char FILE_BEGIN[] = {
        '<',
        static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x97), static_cast<char>(0x8E),
        static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x97), static_cast<char>(0x8E),
        static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x97), static_cast<char>(0x8E),
        'S', 'T', 'A', 'R', 'T',
        0
    };
    static constexpr char FILE_END[] = {
        'E', 'N', 'D',
        static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x97), static_cast<char>(0x8E),
        static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x97), static_cast<char>(0x8E),
        static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x97), static_cast<char>(0x8E),
        '>',
        0
    };

    #pragma pack(push, 1)

    struct Header
    {
        // Size of the pool represented by this file.
        // This is the logical pool size, NOT the file size.
        uint64_t sizeBytes;

        // Pointer to the current owning program.
        // Only valid while the owner is actively running.
        // Never trusted after loading from storage.
        uintptr_t ownerPointer;

        // Name of the owning application.
        char ownerAppName[64];
        uint8_t flags;
        // bit 0 = isElif
        // bit 1 = isSwapMem
        // bit 2 = isBad. Pool should be wiped on recovery.
        // bit 3 = ring geometry follows the payload:
        //         uint32 magic 0x52494E47 ('RING'), head, tail, count.
        uint8_t reserved[7];
    };

    #pragma pack(pop)
}//end namespace
