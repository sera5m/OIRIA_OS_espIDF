/*
 * Unified touch config. One device, two modes.
 * Absolute (touchscreen) and relative (touchpad) share the panel
 * filters. Mode-only fields live in their own struct.
 *
 * Instance one per device. The driver does not own this.
 */
#pragma once

#include "code_stuff/types.h"   // s_bounds_16u
#include <stdint.h>

enum TouchMode : uint8_t {
    TOUCH_MODE_SCREEN = 0,   // absolute points, to the compositor
    TOUCH_MODE_PAD    = 1,   // relative deltas, to HID / remote
};

enum TouchscreenDebugVisualizationMode : uint8_t {
    TSD_NONE                 = 0,
    TSD_CROSSHAIR            = 1u << 0,
    TSD_CIRCLE               = 1u << 1,
    TSD_COLOR_CODED          = 1u << 2,
    TSD_LABEL                = 1u << 3,
    TSD_LABEL_XY             = 1u << 4,
    TSD_LABEL_TIME_HELD      = 1u << 5,
    TSD_SEND_DEBUG_TTY       = 1u << 6,

    TSD_CROSSHAIR_AND_CIRCLE = TSD_CROSSHAIR | TSD_CIRCLE,
    TSD_CIRCLE_AND_LABEL     = TSD_CIRCLE | TSD_LABEL | TSD_LABEL_XY | TSD_LABEL_TIME_HELD,
    TSD_ALL_VISUALS          = TSD_CROSSHAIR | TSD_CIRCLE | TSD_COLOR_CODED |
                               TSD_LABEL | TSD_LABEL_XY | TSD_LABEL_TIME_HELD,
};

inline TouchscreenDebugVisualizationMode operator|(TouchscreenDebugVisualizationMode a,
                                                   TouchscreenDebugVisualizationMode b) {
    return static_cast<TouchscreenDebugVisualizationMode>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline TouchscreenDebugVisualizationMode operator&(TouchscreenDebugVisualizationMode a,
                                                   TouchscreenDebugVisualizationMode b) {
    return static_cast<TouchscreenDebugVisualizationMode>(
        static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline TouchscreenDebugVisualizationMode& operator|=(TouchscreenDebugVisualizationMode& a,
                                                     TouchscreenDebugVisualizationMode b) {
    a = a | b;
    return a;
}
inline bool tsd_has(TouchscreenDebugVisualizationMode mode,
                    TouchscreenDebugVisualizationMode flag) {
    return (static_cast<uint8_t>(mode) & static_cast<uint8_t>(flag)) != 0;
}

/* Shared by both modes. Filters and timing. */
struct TouchPanelConf {
    int16_t  deadzone_x = 0;
    int16_t  deadzone_y = 0;
    int      debounce_time_ms = 0;
    uint8_t  max_touch_points = 5;
    uint32_t poll_interval_us = 20000;          // 50 Hz default
    TouchscreenDebugVisualizationMode debug_vis = TSD_NONE;
};

/* Absolute mode. Panel pixels become screen pixels. */
struct TouchScreenConf {
    uint16_t screen_w = 240;
    uint16_t screen_h = 280;
    float    scale_x = 1.0f;                    // touch_res / screen_res
    float    scale_y = 1.0f;
    uint8_t  compositor_layer = 0;              // 255 = reserved last
};

/* Relative mode. Panel deltas become mouse deltas. */
struct TouchPadConf {
    uint16_t sensitivity = 10;                  // panel px -> mouse units
    bool     reserve_area = false;
    s_bounds_16u reserved = {0, 0, 128, 128};   // xywh, only if reserve_area
    TouchscreenDebugVisualizationMode vis = TSD_CROSSHAIR;
};

/* One config. Mode selects which of the two is live. */
struct TouchConf {
    TouchMode       mode = TOUCH_MODE_SCREEN;
    TouchPanelConf  panel;
    TouchScreenConf screen;                     // used when mode == SCREEN
    TouchPadConf    pad;                        // used when mode == PAD
};
