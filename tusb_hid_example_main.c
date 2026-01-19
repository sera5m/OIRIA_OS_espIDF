


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
//#include <stdint>
#include <string.h>
#include <math.h>
#include "rom/cache.h"
#include "driver/spi_common.h"
#define TAG "ST7789_DMA"

#include "hardware/wiring/wiring.h"
//my libs
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"

#define LCD_BG_COLOR 0x0000  

extern const uint8_t avrclassic_font6x8[]; //hopefully pull font data
// --------------------- GLOBAL STATE ---------------------

uint16_t lcd_background_color= 0x0000;


void app_main(void)
{
	   framebuffer_alloc();   // REQUIRED before any fb_* calls

    spi_init_dma();
    
    lcd_init_simple();
 
    int x1 = 50,  y1 = 50,  vx1 = 3,  vy1 = 2;
    int x2 = 120, y2 = 100, vx2 = -2, vy2 = 3;
    const int size = 40;

    struct vec2_ui16t font6x8 = {6, 8};

    while (1) {
        fb_clear(LCD_BG_COLOR);

        // moving squares
        fb_rect(true,  0, x1, y1, size, size, 0xF800, 0);
        fb_rect(false, 4, x2, y2, size, size, 0x07E0, 0xF00F);

        // text
        fb_draw_text(
            0,              // 0°
            225, 64,
            "bruh lol",
            0xFFFF,
            1,
            NULL,
            avrclassic_font6x8,
            false,
            false,
            LCD_BG_COLOR,
            32,
            font6x8
        );

        fb_draw_text(
            4,              // 90° (driver uses 0/4/8/16)
            200, 20,
            "ROT90",
            0xFFE0,
            1,
            NULL,
            avrclassic_font6x8,
            false,
            false,
            LCD_BG_COLOR,
            16,
            font6x8
        );

        fb_draw_text(
            8,              // 180°
            200, 120,
            "UPSIDE",
            0xF81F,
            2,
            NULL,
            avrclassic_font6x8,
            false,
            false,
            LCD_BG_COLOR,
            16,
            font6x8
        );

        fb_draw_text(
            16,             // 270°
            150, 100,
            "TEST",
            0xF81F,
            2,
            NULL,
            avrclassic_font6x8,
            false,
            false,
            LCD_BG_COLOR,
            16,
            font6x8
        );

        // motion
        x1 += vx1; y1 += vy1;
        x2 += vx2; y2 += vy2;

        if (x1 <= 0 || x1 + size >= SCREEN_W) vx1 = -vx1;
        if (y1 <= 0 || y1 + size >= SCREEN_H) vy1 = -vy1;
        if (x2 <= 0 || x2 + size >= SCREEN_W) vx2 = -vx2;
        if (y2 <= 0 || y2 + size >= SCREEN_H) vy2 = -vy2;

        // push framebuffer to LCD
        fb_display_framebuffer(false, false);
    }
}

