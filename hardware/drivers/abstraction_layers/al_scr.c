// al_scr.c
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "driver/spi_master.h"
#include "hardware/wiring/wiring.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include "hardware/drivers/abstraction_layers/al_scr.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"

// Forward declarations for external display (stubs for now)
// Implement these later if you add an external display
static void external_display_framebuffer(bool a, bool b)
{
    (void)a; (void)b;
    // TODO: implement external display
}

static void external_refresh_screen(void)
{
    // TODO: implement external display refresh
}
/* Active driver pointer */
static const ScreenDriver *active_driver = NULL;

/* Driver instances */
const ScreenDriver onboard_screen_driver = {
    .display_framebuffer = lcd_fb_display_framebuffer,
    .refresh_screen      = lcd_refresh_screen   // ← NOT lcd_refreshScreen
};




/* Select driver at runtime */
void screen_set_driver(const ScreenDriver *driver)
{
    active_driver = driver;
}

/* Abstraction layer */
void display_framebuffer(bool a, bool b)
{
    if (active_driver && active_driver->display_framebuffer)
        active_driver->display_framebuffer(a, b);
}

void refreshScreen(void)
{
    if (active_driver && active_driver->refresh_screen)
        active_driver->refresh_screen();
}
//i forgot what i was doing because i was hungry so chatgpt helped me idk 