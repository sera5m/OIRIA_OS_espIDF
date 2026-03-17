#ifndef AL_SCR_H_
#define AL_SCR_H_


#include <stddef.h>
#include <stdbool.h>

#include <stdint.h>

#include <string.h>
#include "driver/spi_master.h"
#include "hardware/wiring/wiring.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"

// Move this **before** extern "C" so C++ headers are visible
//#include "hardware/drivers/psram_std/psram_std.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    void (*display_framebuffer)(bool, bool);
    void (*refresh_screen)(void);
} ScreenDriver;

/* Public API */
void screen_set_driver(const ScreenDriver *driver);

void display_framebuffer(bool a, bool b);
void refreshScreen(void);

/* Available drivers */
extern const ScreenDriver onboard_screen_driver;
extern const ScreenDriver external_screen_driver;

#ifdef __cplusplus
}
#endif

#endif /* AL_SCR_H_ */