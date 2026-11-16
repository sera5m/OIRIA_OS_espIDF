#pragma once
#include <stdint.h>

// Small style set. Real uint16_t, host order. The driver swaps on write.
// Enough for a watch: background, text, accent, border, highlight, padding.

typedef struct {
    uint16_t bg;
    uint16_t fg;
    uint16_t accent;
    uint16_t border;
    uint16_t highlight;
    uint8_t  pad;
    uint8_t  radius;   // 0 = hard edge; used by button/title/list
} ui_style_t;

// Dark theme matching the phone companion: near-black, lavender, blue.
static inline ui_style_t ui_style_dark(void)
{
    ui_style_t s;
    s.bg        = 0x0000;   // black
    s.fg        = 0xD69A;   // light lavender
    s.accent    = 0x7BFF;   // blue
    s.border    = 0x2945;   // dim blue
    s.highlight = 0xF81F;   // magenta focus
    s.pad       = 4;
    s.radius    = 0;
    return s;
}

static inline ui_style_t ui_style_focus(const ui_style_t* base)
{
    ui_style_t s = *base;
    s.border = base->highlight;
    s.bg     = 0x1082;      // slightly lifted
    return s;
}
