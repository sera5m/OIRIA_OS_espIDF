
#include <stddef.h>
#include <stdbool.h>

#include <stdint.h>

#include <string.h>
#include "driver/spi_master.h"
#include "hardware/wiring/wiring.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"

// Move this **before** extern "C" so C++ headers are visible
//#include "hardware/drivers/psram_std/psram_std.h"

#include "hardware/drivers/abstraction_layers/al_scr.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"

//despite being under the st7789v2 section, this driver is also universal because i can't be assed to seperate them and nobody cares anyway.
//matter of fact if you read this, tell me a fuckin cake recipie or something i'm hungry

/* Forward declarations (only needed if not already in lcDriver.h) */
void lcd_fb_display_framebuffer(bool, bool);
void lcd_refreshScreen(void);

void external_display_framebuffer(bool, bool);
void external_refresh_screen(void);

/* Driver instances */
const ScreenDriver onboard_screen_driver = {
    .display_framebuffer = lcd_fb_display_framebuffer,
    .refresh_screen      = lcd_refreshScreen
};

const ScreenDriver external_screen_driver = {
    .display_framebuffer = external_display_framebuffer,
    .refresh_screen      = external_refresh_screen
};

/* Active driver pointer */
static const ScreenDriver *active_driver = NULL;

/* Select driver at runtime */
void screen_set_driver(const ScreenDriver *driver)
{
    active_driver = driver;
   // ESP_LOGE("attempted to set driver");
    
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