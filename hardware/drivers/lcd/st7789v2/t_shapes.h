#ifndef MAIN_HARDWARE_DRIVERS_LCD_ST7789V2_T_SHAPES_H_
#define MAIN_HARDWARE_DRIVERS_LCD_ST7789V2_T_SHAPES_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "code_stuff/types.h" //incl for the bounds type, msc

#ifdef __cplusplus
extern "C" {
#endif

//====================
// Shape Types---from basial driver type lcdriver, representigng what we need. you will have to add more given changes
//====================
typedef enum {
    SHAPE_NONE = 0,
    SHAPE_RECT, //only normative for now, not highlight rect like around focused windows which use a different primitave to be clear
    SHAPE_LINE,
    SHAPE_CIRCLE,
    SHAPE_TRIANGLE,
    SHAPE_NGON,
    SHAPE_BITMAP,
    SHAPE_TEXT
} fb_shape_type;

//====================
// Bounds incl in types,moved from here because it's universal
//====================


//====================
// Shape Descriptor (PSRAM-friendly)
//====================
// Keep this SMALL. Anything large goes behind a pointer.
typedef struct __attribute__((packed)) {
    s_bounds_16u bounds;

    uint16_t color;
    uint8_t  layer;
    uint8_t  type;

    bool     shown;

    // Pointer to extra data (bitmap pixels, text, vertices, etc.)
    void*    data;

} fb_shape_t;

//====================
// Config
//====================
#ifndef FB_MAX_SHAPES
#define FB_MAX_SHAPES 256
#endif

#ifndef FB_MAX_LAYERS
#define FB_MAX_LAYERS 8
#endif

//====================
// Shape Buffer
//====================
typedef struct {
    fb_shape_t* shapes;                 // PSRAM array
    uint16_t    count;

    // Layer indexing (fast grouping without multiple arrays)
    uint16_t    layer_offsets[FB_MAX_LAYERS];
    uint16_t    layer_counts[FB_MAX_LAYERS];

} fb_shape_buffer_t;


//====================
// Init / Free
//====================
static inline bool fb_shapes_init(fb_shape_buffer_t* fb, uint16_t max_shapes)
{
    if (!fb) return false;

    fb->count = 0;

    for (int i = 0; i < FB_MAX_LAYERS; i++) {
        fb->layer_offsets[i] = 0;
        fb->layer_counts[i]  = 0;
    }

    fb->shapes = (fb_shape_t*)heap_caps_malloc(
        sizeof(fb_shape_t) * max_shapes,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (!fb->shapes) return false;

    return true;
}

static inline void fb_shapes_free(fb_shape_buffer_t* fb)
{
    if (!fb) return;

    if (fb->shapes) {
        heap_caps_free(fb->shapes);
        fb->shapes = NULL;
    }

    fb->count = 0;
}

//====================
// Add Shape
//====================
static inline fb_shape_t* fb_shape_add(
    fb_shape_buffer_t* fb,
    fb_shape_type type,
    s_bounds_16u bounds,
    uint16_t color,
    uint8_t layer
){
    if (!fb || !fb->shapes) return NULL;
    if (fb->count >= FB_MAX_SHAPES) return NULL;
    if (layer >= FB_MAX_LAYERS) return NULL;

    fb_shape_t* s = &fb->shapes[fb->count];

    s->bounds = bounds;
    s->color  = color;
    s->layer  = layer;
    s->type   = type;
    s->shown  = true;
    s->data   = NULL;

    fb->layer_counts[layer]++;
    fb->count++;

    return s;
}

//====================
// Optional: sort by layer (call before render)
//====================
static inline void fb_shapes_sort_by_layer(fb_shape_buffer_t* fb)
{
    if (!fb || fb->count == 0) return;

    // simple insertion sort (small N, low overhead, PSRAM-friendly)
    for (uint16_t i = 1; i < fb->count; i++) {
        fb_shape_t key = fb->shapes[i];
        int j = i - 1;

        while (j >= 0 && fb->shapes[j].layer > key.layer) {
            fb->shapes[j + 1] = fb->shapes[j];
            j--;
        }
        fb->shapes[j + 1] = key;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* MAIN_HARDWARE_DRIVERS_LCD_ST7789V2_T_SHAPES_H_ */