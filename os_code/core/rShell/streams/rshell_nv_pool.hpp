#pragma once

#include <cstdint>
#include <string>


namespace rpool{
    // UTF-8. FILE_BEGIN is 18 bytes, FILE_END is 16. Do not compare them
    // against a 16-byte stack buffer; load will reject every file.
    static constexpr char FILE_BEGIN[] = "<START";
    static constexpr char FILE_END[]   = "END>";

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
        // bit 2 = isBad? Indicates the pool should be considered invalid or wiped during initialization/startup recovery.
        // bit 3 = ring geometry follows the payload (uint32 magic 'RING', head, tail, count)
        uint8_t reserved[7];// Pad to an 8-byte boundary.
    };//end struct

    #pragma pack(pop)
}//end namespace



    // -------------------------------------------------------------------------
    // RPOOL File Header
    //
     //
    // <document-emoji x3>START
    // [packed Header]
    // [pool data]
    // [optional ring geometry if flags bit 3]
    // END<document-emoji x3>>
    // -------------------------------------------------------------------------


