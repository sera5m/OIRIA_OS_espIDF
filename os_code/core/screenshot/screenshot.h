/*
 * Dump the front framebuffer to a BMP on the microSD.
 * Path is /sdcard/shots/shot_<n>.bmp. Returns the path written, or null.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Writes the current front buffer. Returns the path, or null on failure. */
const char* screenshot_save(void);

#ifdef __cplusplus
}
#endif
