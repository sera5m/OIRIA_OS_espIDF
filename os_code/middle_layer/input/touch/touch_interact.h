/*
 * Touchscreen mode.
 * Pointer starts at the reserved (-32,-32). A single click repositions it
 * to the touch point. A double tap on the same element selects.
 * Double-tap window is 500 ms. Pointer hides after 8 s of no activity.
 *
 * Focused panel: select the element.
 * Non-focused panel: bring it to the front (layer 0).
 */
#pragma once

#include <stdint.h>
#include "os_code/core/window_env/ui/ui_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at boot. */
void touch_interact_init(void);

/* One contact. x,y are screen pixels. down is true on press. */
void touch_interact_point(int16_t x, int16_t y, int down);

/* Current pointer. Hidden means do not draw. */
void touch_interact_pointer(int16_t* x, int16_t* y, int* visible);

#ifdef __cplusplus
}
#endif
