


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
#include <vector>
#include "rom/cache.h"
#include "driver/spi_common.h"
#define TAG "ST7789_DMA"

#include "hardware/wiring/wiring.h"
#include "hardware/drivers/abstraction_layers/al_scr.h"
//my libs
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include "os_code/core/window_env/MWenv.hpp"

#include "hardware/drivers/encoders/ky040_driver.hpp"

// Encoder handles
static ky040_handle_t enc_left = nullptr;
static ky040_handle_t enc_right = nullptr;

// Tick counters (accumulated deltas)
static int32_t ticks_x = 0;  // left encoder
static int32_t ticks_y = 0;  // right encoder

#define LCD_BG_COLOR 0x0000  

extern const uint8_t avrclassic_font6x8[]; //hopefully pull font data
// --------------------- GLOBAL STATE ---------------------

uint16_t lcd_background_color= 0x0000;


static void on_encoder_tick(void* user_ctx, int delta) {
    // user_ctx is 0 for left, 1 for right
    uintptr_t which = (uintptr_t)user_ctx;

    if (which == 0) {
        ticks_x += delta;
        ESP_LOGI("ENC", "Left encoder: %ld", ticks_x);
    } else if (which == 1) {
        ticks_y += delta;
        ESP_LOGI("ENC", "Right encoder: %ld", ticks_y);
    }
}

void cpp_main(void)
{
	    // Initialize encoders
    ky040_config_t cfg_left = {
        .clk_pin         = ENCODER0_CLK_PIN,
        .dt_pin          = ENCODER0_DT_PIN,
        .sw_pin          = ENCODER0_SW_PIN,         // or KY040_PIN_UNUSED if you don't use button
        .detents_per_rev = 20,
        .on_twist        = on_encoder_tick,
        .user_ctx        = (void*)(uintptr_t)0      // left = 0
    };

    esp_err_t err = ky040_new(&cfg_left, &enc_left);
    if (err != ESP_OK) {
        ESP_LOGE("ENC", "Left encoder init failed: %s", esp_err_to_name(err));
    }

    ky040_config_t cfg_right = {
        .clk_pin         = ENCODER1_CLK_PIN,
        .dt_pin          = ENCODER1_DT_PIN,
        .sw_pin          = ENCODER1_SW_PIN,
        .detents_per_rev = 20,
        .on_twist        = on_encoder_tick,
        .user_ctx        = (void*)(uintptr_t)1      // right = 1
    };

    err = ky040_new(&cfg_right, &enc_right);
    if (err != ESP_OK) {
        ESP_LOGE("ENC", "Right encoder init failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI("ENC", "Encoders initialized");
    
	screen_set_driver(&onboard_screen_driver); //set the screen to onboard lcd. MUST go first OR SILENT FAIL
	   framebuffer_alloc();   // REQUIRED before any fb_* calls

    spi_init_dma();
    
    lcd_init_simple();
 
    int x1 = 50,  y1 = 50,  vx1 = 3,  vy1 = 2;
    int x2 = 120, y2 = 100, vx2 = -2, vy2 = 3;
    const int size = 40;

  // extern struct vec2_ui16t font6x8 = {6, 8};
auto win = std::make_shared<Window>(
	
        WindowCfg{
            .Posx = 10,
            .Posy = 20,
            .win_width  = 240,
            .win_height = 160,
            .win_rotation = 1,
            .borderless = false,
            .TextSizeMult = 1,
            .BgColor = 0x0010,          // dark blue-ish
            .WinTextColor = 0xFFFF,
            .UpdateRate = 1.0f          // doesn't matter for manual draw
        },
        
        "<|u|>Underlined text<|/u|> normal again<|s|><|n|>Strikethrough example<|/s|><|hl=0x07E0|><|n|>Highlighted green background<|/hl|><|size=2|><|b|>Bold big text<|/b|><|/size|><|n|><|i|>Italic text (placeholder)<|/i|>Mixed: normal <|u|>under<|/u|><|n|> <|hl=0xF800|>red highlight<|/hl|> <|s|>strike<|/s|> end"
        
        
    );

    win->WinDraw();
    
      while (1) {
        // Poll encoders → this calls the callback when ticks happen
        if (enc_left)  ky040_poll(enc_left);
        if (enc_right) ky040_poll(enc_right);

        fb_clear(LCD_BG_COLOR);

        // moving squares (keep your animation)
        fb_rect(true, 0, x1, y1, size, size, 0xF800, 0);
        fb_rect(false, 4, x2, y2, size, size, 0x07E0, 0xF00F);

        // Format the display text with current tick values
        char buf[256];
        snprintf(buf, sizeof(buf),
            "Encoder ticks:\n"
            "X: %ld   Y: %ld\n\n"
            "<|size=2|><|b|>Use knobs to change!<|/b|><|/size|>",
            ticks_x, ticks_y
        );

        // Update window content (replaces old text)
        win->SetText(buf);

        // Redraw window
        win->WinDraw();

        // motion (your bouncing squares)
        x1 += vx1; y1 += vy1;
        x2 += vx2; y2 += vy2;

        if (x1 <= 0 || x1 + size >= SCREEN_W) vx1 = -vx1;
        if (y1 <= 0 || y1 + size >= SCREEN_H) vy1 = -vy1;
        if (x2 <= 0 || x2 + size >= SCREEN_W) vx2 = -vx2;
        if (y2 <= 0 || y2 + size >= SCREEN_H) vy2 = -vy2;

        // Push to LCD
        display_framebuffer(true, false);

        // Don't spin CPU too hard
        vTaskDelay(pdMS_TO_TICKS(10));   // ~100 Hz – plenty for encoders
    }
}

// load main cpp app because entrypoint seems to be only in c
extern "C" void app_main(void)
{
    cpp_main();
}
