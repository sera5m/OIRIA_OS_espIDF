#include "MS_fileviewwerapp.hpp"
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
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_master.h"
#include "hardware/drivers/sd_card/d_sdc.h"
#include "os_code/applications/fileviewwer/MS_file_viewwer.hpp"
static const char* TAG = "File_Viewwer_App";

// ===================================================================
// Fast helpers (no snprintf)
// ===================================================================


 //this is stupid but i'll just alloc it here. gotta figure out how to get the alloc on runtime
// ===================================================================

File_Viewwer_App::File_Viewwer_App(const ApplicationConfig& cfg)
    : AppBase(cfg)
{
    appTickRateHZ = 5;  // it's a clock. how often do you need to see the miliseconds. come on.
}

void File_Viewwer_App::on_start()
{
    ESP_LOGI(TAG, "fileviewwerapp started – creating window");

    fv_app_window = std::make_shared<Window>(
        WindowCfg{
            .Posx = static_cast<uint16_t>(0),
            .Posy = static_cast<uint16_t>(0),
            .Layer = 0,
            .renderPriority = 0,
            .win_width = static_cast<uint16_t>(v_env.clamped_screen_dim_w),
            .win_height = static_cast<uint16_t>(v_env.clamped_screen_dim_h),
            .win_rotation = 1,
            .AutoAlignment = false,
            .WrapText = true,
            .borderless = false,
            .ShowNameAtTopOfWindow = false,
            .TextSizeMult = 1,
            .name = {0},
            .optionsbitmask = 0,
            .BorderColor = 0x12FF,
            .BgColor = 0xAA00,
            .Bg_secondaryColor = 0xFF34,
            .WinTextColor = 0xFFFF,
            .backgroundType = BgFillType::waves,
            .UpdateRate = 1.0f
        },
        "time"
    );
    
    WindowManager::getInstance().registerWindow(fv_app_window);
    bind_main_window(fv_app_window);
    ESP_LOGI(TAG, "fv screen Window registered");
    ESP_LOGI(TAG, "attemtping fv screen draw.....");
    on_draw();  // initial draw
}

void File_Viewwer_App::on_stop()
{
    ESP_LOGI(TAG, "fileviewwerapp stopped");
    if (fv_app_window) fv_app_window.reset();
}

void File_Viewwer_App::on_pause()  { ESP_LOGI(TAG, "fileviewwerapp paused"); }
void File_Viewwer_App::on_resume() { ESP_LOGI(TAG, "fileviewwerapp resumed"); }

void File_Viewwer_App::on_draw() {
    if (!fv_app_window) return;
    
    static int last_second = -1;
    int current_second = v_env.displayTime.ss;
    
      
        
        ESP_LOGI(TAG, "Time string: %s", time_str);  // Debug
        
       // fv_app_window->SetText(time_str);
        fv_app_window->dirty = true;
        last_second = current_second;
    
}

void File_Viewwer_App::tick_app(uint32_t delta_ms)
{
    static int call_count = 0;
    ESP_LOGI(TAG, "tick_app called #%d, delta_ms=%lu", ++call_count, delta_ms);  // ← ADD THIS
    
    static uint32_t accumulator = 0;
    accumulator += delta_ms;

    if (accumulator >= 500) {
        ESP_LOGI(TAG, "Calling on_draw from tick_app");  // ← ADD THIS
        on_draw();
        accumulator = 0;
    }
}

void File_Viewwer_App::receive_event_input(const void* event)
{
    ESP_LOGI(TAG, "fv received input event");
}

void File_Viewwer_App::suspend()
{
    ESP_LOGI(TAG, "fileviewwerapp suspending");
    on_pause();
}

void File_Viewwer_App::force_close()
{
    ESP_LOGI(TAG, "fileviewwerapp force close");
    on_stop();
    stop_task();
}