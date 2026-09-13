#pragma once
// 2×2 RECT sparkline on a Canvas (SHAPE_LINE can't go up-left: w/h are unsigned).
#include "os_code/core/window_env/Canvas.hpp"
#include "hardware/drivers/lcd/st7789v2/t_shapes.h"

struct TracePlot {
    static constexpr int MAX = 64;
    fb_shape_t* dots[MAX]{};
    int n = 0;
    int w = 0, h = 0;

    void init(Canvas* c, int width, int height, int count, uint16_t color) {
        w = width;
        h = height;
        n = count;
        if (n > MAX) n = MAX;
        if (n < 2) n = 2;
        for (int i = 0; i < n; i++) {
            s_bounds_16u b{};
            b.x = (uint16_t)((i * (w - 2)) / (n - 1));
            b.y = (uint16_t)(h / 2);
            b.w = 2;
            b.h = 2;
            dots[i] = c->AddShape(SHAPE_RECT, b, color, 1);
        }
    }

    void set_q15(const int16_t* s, int ns) {
        if (!s) return;
        if (ns > n) ns = n;
        for (int i = 0; i < ns; i++) {
            if (!dots[i]) continue;
            int x = (i * (w - 2)) / (n - 1);
            int y = (h / 2) - ((int)s[i] * (h / 2 - 3)) / 32767;
            if (y < 0) y = 0;
            if (y > h - 2) y = h - 2;
            dots[i]->bounds.x = (uint16_t)x;
            dots[i]->bounds.y = (uint16_t)y;
            dots[i]->shown = true;
        }
    }

    void set_mv(const int16_t* s, int ns, int mn, int mx) {
        if (!s || ns <= 0) return;
        if (mx <= mn) mx = mn + 1;
        if (ns > n) ns = n;
        for (int i = 0; i < ns; i++) {
            if (!dots[i]) continue;
            int x = (i * (w - 2)) / (n - 1);
            int y = h - 3 - (int)(((int32_t)s[i] - mn) * (h - 6) / (mx - mn));
            if (y < 0) y = 0;
            if (y > h - 2) y = h - 2;
            dots[i]->bounds.x = (uint16_t)x;
            dots[i]->bounds.y = (uint16_t)y;
            dots[i]->shown = true;
        }
    }
};
