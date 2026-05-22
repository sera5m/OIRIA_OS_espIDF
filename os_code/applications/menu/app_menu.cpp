#include "MS_watchapp.hpp"
#include "esp_log.h"
#include <stdint.h>
#include "esp_timer.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include <string>
#include <memory>
#include <algorithm>
#include <variant>
#include "code_stuff/types.h"
#include <math.h>
#include "hardware/wiring/wiring.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/spi_common.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "rom/cache.h"
#include <string.h>
#include "hardware/drivers/abstraction_layers/al_scr.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "os_code/core/window_env/wenv_basicThemes.h"
#include <vector>
#include "../../../hardware/drivers/psram_std/psram_std.hpp"
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/core/rShell/s_hell.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "code_stuff/helperfunctions.hpp"

static const char* TAG = "app_launcher_menu";

// ===================================================================
// Fast helpers (no snprintf)
// ===================================================================


 // ===================================================================

app_launcher_menu::app_launcher_menu(const ApplicationConfig& cfg)
    : AppBase(cfg)
{
    appTickRateHZ = 5;  // it's a clock. how often do you need to see the miliseconds. come on.
}

void app_launcher_menu::on_start()
{
    ESP_LOGI(TAG, "app_launcher_menu started – creating window");

    menu_window = std::make_shared<Window>(
        WindowCfg{
            .Posx = static_cast<uint16_t>(0),
            .Posy = static_cast<uint16_t>(0),
            .Layer = 0,
            .renderPriority = 0,
            .win_width = static_cast<uint16_t>(v_env.clamped_screen_dim_w),
            .win_height = static_cast<uint16_t>((v_env.clamped_screen_dim_h)),
            .win_rotation = 1,
            .AutoAlignment = false,
            .WrapText = true,
            .borderless = false,
            .ShowNameAtTopOfWindow = false,
            .TextSizeMult = 1,
            .name = {0},
            .optionsbitmask = 0,
            .BorderColor = 0x12FF,
            .BgColor = 0xB00B,
            .Bg_secondaryColor = 0xABCD,
            .WinTextColor = 0xAFFA,
            .backgroundType = BgFillType::Solid,
            .UpdateRate = 1.0f
        },
        "time"
    );
    
    WindowManager::getInstance().registerWindow(menu_window);
    bind_main_window(menu_window);
    ESP_LOGI(TAG, "app_launcher_menu Window registered");
    ESP_LOGI(TAG, "attemtping watch screen draw.....");
    on_draw();  // initial draw
}

void app_launcher_menu::on_stop()
{
    ESP_LOGI(TAG, "WatchApp stopped");
    if (menu_window) menu_window.reset();
}

void app_launcher_menu::on_pause()  { ESP_LOGI(TAG, "WatchApp paused"); }
void app_launcher_menu::on_resume() { ESP_LOGI(TAG, "WatchApp resumed"); }

void app_launcher_menu::on_draw() {
    if (!menu_window) return;
    
    static int last_second = -1;
    int current_second = v_env.displayTime.ss;
    
   
       // ESP_LOGI(TAG, "Time string: %s", time_str);  // Debug
        
      //  menu_window->SetText(time_str);
       // menu_window->dirty = true;
       // last_second = current_second;
    }


void app_launcher_menu::tick_app(uint32_t delta_ms)
{
    static int call_count = 0;
    ESP_LOGI(TAG, "tick_app called #%d, delta_ms=%lu", ++call_count, delta_ms);  // ← ADD THIS
    
    static uint32_t accumulator = 0;
    accumulator += delta_ms;

    if (accumulator >= 100) {
        ESP_LOGI(TAG, "Calling on_draw from tick_app");  
        on_draw();
        accumulator = 0;
    }
}

void app_launcher_menu::receive_event_input(const void* event)
{
    ESP_LOGI(TAG, "Watch received input event");
}

void app_launcher_menu::suspend()
{
    ESP_LOGI(TAG, "menu suspending");
    on_pause();
}

void app_launcher_menu::force_close()
{
    ESP_LOGI(TAG, " force close");
    on_stop();
    stop_task();
}