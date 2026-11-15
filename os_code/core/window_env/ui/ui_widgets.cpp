#include "ui_widgets.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"

void ui_label(int x, int y, const char* text,
              const ui_style_t* style, ui_font_size_t size)
{
    if (!text || !style) return;
    fontdata f = ui_font(size);
    fb_draw_text(0, x, y, text,
                 style->fg, 1,
                 0, false, 0,
                 0, f);
}

void ui_bar(int x, int y, int w, int h,
            int value, int max,
            const ui_style_t* style)
{
    if (!style || w <= 0 || h <= 0) return;
    if (max <= 0) max = 1;
    if (value < 0) value = 0;
    if (value > max) value = max;

    // Track
    fb_rect(true, 1, x, y, w, h, style->bg, style->border);

    // Fill
    int fill_w = (int)((int32_t)(w - 2) * value / max);
    if (fill_w > 0) {
        fb_rect(true, 0, x + 1, y + 1, fill_w, h - 2,
                style->accent, 0);
    }
}

void ui_button(int x, int y, int w, int h,
               const char* text, bool focused,
               const ui_style_t* style, ui_font_size_t size)
{
    if (!style) return;
    ui_style_t s = focused ? ui_style_focus(style) : *style;

    fb_rect(true, 1, x, y, w, h, s.bg, s.border);

    if (text) {
        fontdata f = ui_font(size);
        int tx = x + s.pad;
        int ty = y + (h - (int)f.fcs.y) / 2;
        if (ty < y) ty = y;
        fb_draw_text(0, tx, ty, text,
                     s.fg, 1,
                     0, false, 0,
                     0, f);
    }
}
