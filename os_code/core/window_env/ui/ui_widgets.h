#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ui_style.h"
#include "ui_font.h"

#ifdef __cplusplus
extern "C" {
#endif

// Label. Uses the existing text primitive. Color is host-order;
// the driver swaps it on the way into the framebuffer.
void ui_label(int x, int y, const char* text,
              const ui_style_t* style, ui_font_size_t size);

// Bar with fill. value/max is the fill fraction.
// background is style->border, fill is style->accent.
void ui_bar(int x, int y, int w, int h,
            int value, int max,
            const ui_style_t* style);

// Button. focused changes the border to highlight.
// Returns nothing; the caller owns the selection model.
void ui_button(int x, int y, int w, int h,
               const char* text, bool focused,
               const ui_style_t* style, ui_font_size_t size);

#ifdef __cplusplus
}
#endif
