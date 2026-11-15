#pragma once
#include <stdint.h>
#include "hardware/drivers/lcd/fonts/font_basic_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Size request. 8 maps to 6x8, 16 maps to 8x16.
// DIGITS is the chunky watch-face set (0-9 and colon).
typedef enum {
    UI_FONT_8      = 8,
    UI_FONT_16     = 16,
    UI_FONT_DIGITS = 100
} ui_font_size_t;

// Named fonts. The size enum is the common path; this is for when
// an app wants a specific face.
typedef enum {
    UI_FACE_AVR6,
    UI_FACE_AVR16,
    UI_FACE_DIGITS
} ui_font_face_t;

fontdata ui_font(ui_font_size_t size);
fontdata ui_font_face(ui_font_face_t face);

#ifdef __cplusplus
}
#endif
