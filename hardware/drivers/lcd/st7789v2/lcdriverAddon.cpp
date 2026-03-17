
// lcDriver.c
#include "lcDriver.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>               // ← added for sqrtf, cosf, sinf

#include <stdint.h>
#include <stdbool.h>

#include "../../psram_std/psram_std.hpp" //my custom work for psram stdd things
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"

static const char* TAG = "LCDAddon";

//PSRAMSTRING variant 
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
) {

    int ax=1, ay=0;
    int ux=0, uy=1;

    switch (angle & 0x1F) {
        case 0:  ax=1; ay=0;  ux=0;  uy=1;  break;
        case 4:  ax=0; ay=1;  ux=-1; uy=0;  break;
        case 8:  ax=-1;ay=0;  ux=0;  uy=-1; break;
        case 12: ax=0; ay=-1; ux=1;  uy=0;  break;
        default: ax=1; ay=0;  ux=0;  uy=1;  break;
    }

    int cursor = 0;

    for (size_t i = 0; i < str.length(); i++) {

        char c = str[i];

        if (c < ' ' || c > '~') {
            cursor++;
            continue;
        }

        const uint8_t* glyph =
            &font[(c - ' ') * fontSize.x];

        for (int col = 0; col < fontSize.x; col++) {

            uint8_t bits = glyph[col];

            int row = 0;

            while (row < fontSize.y) {

                if (!(bits & (1 << row))) {
                    row++;
                    continue;
                }

                int start = row;

                while (row < fontSize.y && (bits & (1 << row)))
                    row++;

                int end = row - 1;

                int span_len = (end - start + 1) * size;

                int gx = (cursor * fontSize.x + col) * size;
                int gy = start * size;

                int px = x + gx * ax + gy * ux;
                int py = y + gx * ay + gy * uy;

                for (int s = 0; s < span_len; s++) {
                    for (int t = 0; t < size; t++) {

                        int sx = px + s * ux + t * ax;
                        int sy = py + s * uy + t * ay;

                        if (sx >= 0 && sx < SCREEN_W &&
                            sy >= 0 && sy < SCREEN_H)
                            framebuffer[sy * SCREEN_W + sx] = color;
                    }
                }
            }
        }

        cursor++;
    }
}