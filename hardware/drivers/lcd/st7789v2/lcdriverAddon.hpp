
// lcdriverAddon.hpp
#pragma once

#include "hardware/drivers/lcd/st7789v2/lcDriver.h"  // core C driver
#include "hardware/drivers/psram_std/psram_std.hpp"   // safe here (C++ only)

#ifdef __cplusplus

void fb_draw_ptext(
    uint8_t angle,
    int x, int y,
    const stdpsram::String& str,
    uint16_t color,
    uint8_t size,
    const uint8_t* font,
    uint8_t transparency,
    bool drawblocksforbackground,
    uint16_t blockBackground_color,
    uint16_t maxTLenBeforeAutoWrapToNextLine,
    struct fontcharsize fontSize
);

#endif