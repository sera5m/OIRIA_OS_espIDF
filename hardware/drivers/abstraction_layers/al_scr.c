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
#include "hardware/drivers/lcd/crt_rca_mono/crt_rm.h"
#include "hardware/drivers/lcd/st7789v2/lcd_color.h"

static const ScreenDriver *active_driver = NULL;

static void crt_display_framebuffer(bool a, bool b)
{
    (void)a;
    (void)b;
    crt_rm_present();
}

static void crt_refresh_screen(void)
{
    crt_rm_present();
}

/* Driver instances */
const ScreenDriver onboard_screen_driver = {
    .display_framebuffer = lcd_fb_display_framebuffer,
    .refresh_screen      = lcd_refresh_screen,
    .byteswap            = true   // ST7789
};

const ScreenDriver external_screen_driver = {
    .display_framebuffer = crt_display_framebuffer,
    .refresh_screen      = crt_refresh_screen,
    .byteswap            = false  // CRT matches host order
};

/* Select driver at runtime. Sets the color swap from the driver's flag. */
void screen_set_driver(const ScreenDriver *driver)
{
    active_driver = driver;
    if (driver) {
        lcd_set_byteswap(driver->byteswap);
    }
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
