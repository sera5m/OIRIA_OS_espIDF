#pragma once

#include <cstdint>
#include <string>


namespace rpool{
    static constexpr char FILE_BEGIN[] = "<🗎🗎🗎START";
    static constexpr char FILE_END[]   = "END🗎🗎🗎>";

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
        // Should correspond to an entry in the application's table.
        char ownerAppName[64]; //this should be a std::string
        uint8_t flags;
        // bit 0 = isElif
        // bit 1 = isSwapMem
        // bit 2 = isBad? Indicates the pool should be considered invalid or wipedduring initialization/startup recovery.
        uint8_t reserved[7];// Pad to an 8-byte boundary.
    };//end struct

    #pragma pack(pop)
}//end namespace



    // -------------------------------------------------------------------------
    // RPOOL File Header
    //
     //
    // <🗎🗎🗎
    // START
    // pbgn
    // ~sz[size_bytes]
    // ~pownr[pointer]
    // ~OWNERAPPNAME[name]
    // ~isElif[bool]
    // ~isSwapMem[bool]
    // ~isBad[bool]
    //
    // [pool data]
    //
    // END🗎🗎🗎>
    // -------------------------------------------------------------------------


