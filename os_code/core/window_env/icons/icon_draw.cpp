#include "icon_draw.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"

void icon_draw_1bit(int x, int y, int w, int h, const uint8_t* bits, uint16_t color)
{
    if (!bits || !framebuffer) return;
    int i = 0;
    for (int row = 0; row < h; ++row) {
        int py = y + row;
        if (py < 0 || py >= SCREEN_H) { i += w; continue; }
        for (int col = 0; col < w; ++col, ++i) {
            int px = x + col;
            if (px < 0 || px >= SCREEN_W) continue;
            int byte = i >> 3;
            int bit  = 7 - (i & 7);
            if (bits[byte] & (1 << bit))
                framebuffer[py * SCREEN_W + px] = color;
        }
    }
}
