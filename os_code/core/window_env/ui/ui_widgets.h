#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ui_style.h"
#include "ui_font.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_label(int x, int y, const char* text,
              const ui_style_t* style, ui_font_size_t size);

void ui_bar(int x, int y, int w, int h,
            int value, int max,
            const ui_style_t* style);

void ui_button(int x, int y, int w, int h,
               const char* text, bool focused,
               const ui_style_t* style, ui_font_size_t size);

// Circular progress. value/max is the filled arc, starting at 12 o'clock.
void ui_arc(int cx, int cy, int r,
            int value, int max,
            const ui_style_t* style);

// Pie. values[] are the slice sizes. n is the count.
// Colors cycle through a small fixed set if colors is null.
void ui_pie(int cx, int cy, int r,
            const int* values, int n,
            const uint16_t* colors,
            const ui_style_t* style);

// Line series. samples are host values. n is the count.
// The series is scaled to fit the box.
void ui_spark(int x, int y, int w, int h,
              const int16_t* samples, int n,
              const ui_style_t* style);

#ifdef __cplusplus
}
#endif
