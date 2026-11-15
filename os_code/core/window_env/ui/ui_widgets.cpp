#include "ui_widgets.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.141592653589
#endif

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

    fb_rect(true, 1, x, y, w, h, style->bg, style->border);

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

// Bresenham circle, then fill the arc by angle test.
static void plot_arc(int cx, int cy, int r, float a0, float a1, uint16_t color)
{
    // Walk the circle and keep points whose angle is in [a0, a1).
    int x = 0, y = r;
    int d = 3 - 2 * r;
    auto in_arc = [&](int px, int py) {
        float a = atan2f((float)(px - cx), (float)(cy - py)); // 0 at top, clockwise-ish
        if (a < 0) a += 2 * M_PI;
        return a >= a0 && a < a1;
    };
    auto put = [&](int px, int py) {
        if (in_arc(px, py)) fb_line(px, py, px, py, color);
    };
    while (y >= x) {
        put(cx + x, cy + y); put(cx - x, cy + y);
        put(cx + x, cy - y); put(cx - x, cy - y);
        put(cx + y, cy + x); put(cx - y, cy + x);
        put(cx + y, cy - x); put(cx - y, cy - x);
        x++;
        if (d > 0) { y--; d = d + 4 * (x - y) + 10; }
        else       { d = d + 4 * x + 6; }
    }
}

void ui_arc(int cx, int cy, int r,
            int value, int max,
            const ui_style_t* style)
{
    if (!style || r <= 0) return;
    if (max <= 0) max = 1;
    if (value < 0) value = 0;
    if (value > max) value = max;

    // Track
    plot_arc(cx, cy, r, 0, 2 * M_PI, style->border);
    // Fill
    float frac = (float)value / (float)max;
    plot_arc(cx, cy, r, 0, frac * 2 * M_PI, style->accent);
}

void ui_pie(int cx, int cy, int r,
            const int* values, int n,
            const uint16_t* colors,
            const ui_style_t* style)
{
    if (!style || !values || n <= 0 || r <= 0) return;
    int total = 0;
    for (int i = 0; i < n; i++) total += values[i] > 0 ? values[i] : 0;
    if (total <= 0) return;

    static const uint16_t fallback[] = { 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF };
    float a = 0;
    for (int i = 0; i < n; i++) {
        int v = values[i] > 0 ? values[i] : 0;
        float slice = (float)v / (float)total * 2 * M_PI;
        uint16_t c = colors ? colors[i] : fallback[i % 6];
        // Fill by walking radii
        for (int rr = 1; rr <= r; rr++) {
            plot_arc(cx, cy, rr, a, a + slice, c);
        }
        a += slice;
    }
    plot_arc(cx, cy, r, 0, 2 * M_PI, style->border);
}

void ui_spark(int x, int y, int w, int h,
              const int16_t* samples, int n,
              const ui_style_t* style)
{
    if (!style || !samples || n < 2 || w <= 1 || h <= 1) return;

    int16_t lo = samples[0], hi = samples[0];
    for (int i = 1; i < n; i++) {
        if (samples[i] < lo) lo = samples[i];
        if (samples[i] > hi) hi = samples[i];
    }
    if (hi == lo) hi = lo + 1;

    fb_rect(true, 0, x, y, w, h, style->bg, 0);

    for (int i = 0; i < n - 1; i++) {
        int x0 = x + (int)((int32_t)i * (w - 1) / (n - 1));
        int x1 = x + (int)((int32_t)(i + 1) * (w - 1) / (n - 1));
        int y0 = y + h - 1 - (int)((int32_t)(samples[i] - lo) * (h - 1) / (hi - lo));
        int y1 = y + h - 1 - (int)((int32_t)(samples[i + 1] - lo) * (h - 1) / (hi - lo));
        fb_line(x0, y0, x1, y1, style->accent);
    }
}
