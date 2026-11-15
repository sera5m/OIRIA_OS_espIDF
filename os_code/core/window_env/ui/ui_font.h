#pragma once
#include <stdint.h>
#include "hardware/drivers/lcd/fonts/font_basic_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Size request. 8 maps to the existing 6x8, 16 maps to the existing 8x16.
// Anything else falls back to the closest we have.
typedef enum {
    UI_FONT_8  = 8,
    UI_FONT_16 = 16
} ui_font_size_t;

// Returns a fontdata the existing fb_draw_text already understands.
fontdata ui_font(ui_font_size_t size);

#ifdef __cplusplus
}
#endif
