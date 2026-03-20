
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
/*
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
}*/

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

   


    // First pass: Draw background blocks if requested
    if (drawblocksforbackground) {
        for (size_t i = 0; i < str.length(); i++) {
            char c = str[i];
            
            if (c < ' ' || c > '~') {
                cursor++;
                continue;
            }
            
            // Calculate block position with rotation
            int block_width = fontSize.x * size;
            int block_height = fontSize.y * size;
            
            // Get rotated block corners
            int gx_base = cursor * block_width;
            int gy_base = 0;
            
            // Calculate all four corners of the block
            int corners[4][2] = {
                {x + gx_base * ax + gy_base * ux, y + gx_base * ay + gy_base * uy},  // top-left
                {x + (gx_base + block_width) * ax + gy_base * ux, y + (gx_base + block_width) * ay + gy_base * uy},  // top-right
                {x + gx_base * ax + (gy_base + block_height) * ux, y + gx_base * ay + (gy_base + block_height) * uy},  // bottom-left
                {x + (gx_base + block_width) * ax + (gy_base + block_height) * ux, 
                 y + (gx_base + block_width) * ay + (gy_base + block_height) * uy}  // bottom-right
            };
            
            // Find the bounding box of the rotated rectangle
            int min_x = corners[0][0], max_x = corners[0][0];
            int min_y = corners[0][1], max_y = corners[0][1];
            
            for (int j = 1; j < 4; j++) {
                if (corners[j][0] < min_x) min_x = corners[j][0];
                if (corners[j][0] > max_x) max_x = corners[j][0];
                if (corners[j][1] < min_y) min_y = corners[j][1];
                if (corners[j][1] > max_y) max_y = corners[j][1];
            }
            
            // Draw the filled rotated rectangle using a simple bounding box approach
            // For more accurate rotation, you might want to implement a proper rotated rectangle fill
            for (int py = min_y; py <= max_y; py++) {
                for (int px = min_x; px <= max_x; px++) {
                    if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
                        // Optional: Add inside-rectangle check for better accuracy
                        framebuffer[py * SCREEN_W + px] = blockBackground_color;
                    }
                }
            }
            
            cursor++;
        }
    }

    // Second pass: Draw text (your existing code, reset cursor)
    cursor = 0;
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