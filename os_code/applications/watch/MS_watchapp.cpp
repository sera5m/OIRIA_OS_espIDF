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

static const char* TAG = "MyWatchApp";

// ===================================================================
// Fast helpers (no snprintf)
// ===================================================================
inline char* write_small_int(char* p, int value)
{
    if (value == 0) {
        *p++ = '0';
        return p;
    }
    if (value >= 100) {
        *p++ = '0' + (value / 100);
        value %= 100;
    }
    if (value >= 10) {
        *p++ = '0' + (value / 10);
        value %= 10;
    }
    *p++ = '0' + value;
    return p;
}

char time_str[64];
 //this is stupid but i'll just alloc it here. gotta figure out how to get the alloc on runtime
// ===================================================================

MyWatchApp::MyWatchApp(const ApplicationConfig& cfg)
    : AppBase(cfg)
{
    appTickRateHZ = 5;  // it's a clock. how often do you need to see the miliseconds. come on.
}

void MyWatchApp::on_start()
{
    ESP_LOGI(TAG, "WatchApp started – creating window");

    watch_window = std::make_shared<Window>(
        WindowCfg{
            .Posx = static_cast<uint16_t>(0),
            .Posy = static_cast<uint16_t>(v_env.screen_dim_w / 4),
            .Layer = 0,
            .renderPriority = 0,
            .win_width = static_cast<uint16_t>(v_env.screen_dim_w),
            .win_height = static_cast<uint16_t>((v_env.screen_dim_h / 2)),
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
    
    WindowManager::getInstance().registerWindow(watch_window);
    ESP_LOGI(TAG, "watch screen Window registered");
    ESP_LOGI(TAG, "attemtping watch screen draw.....");
    on_draw();  // initial draw
}

void MyWatchApp::on_stop()
{
    ESP_LOGI(TAG, "WatchApp stopped");
    if (watch_window) watch_window.reset();
}

void MyWatchApp::on_pause()  { ESP_LOGI(TAG, "WatchApp paused"); }
void MyWatchApp::on_resume() { ESP_LOGI(TAG, "WatchApp resumed"); }

void MyWatchApp::on_draw() {
    if (!watch_window) return;
    
    static int last_second = -1;
    int current_second = v_env.displayTime.ss;
    
    ESP_LOGI(TAG, "on_draw: current_second=%d, last_second=%d", current_second, last_second);  // ← ADD THIS
    
    if (current_second != last_second) {
        snprintf(time_str, sizeof(time_str), "<|size=3|>Time: %02d:%02d:%02d", 
                 v_env.displayTime.hh, v_env.displayTime.mm, v_env.displayTime.ss);
        
        ESP_LOGI(TAG, "Updating text to: %s", time_str);  // ← ADD THIS
        
        watch_window->SetText(time_str);
        watch_window->dirty = true;
        last_second = current_second;
    }
}
void MyWatchApp::tick_app(uint32_t delta_ms)
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

void MyWatchApp::receive_event_input(const void* event)
{
    ESP_LOGI(TAG, "Watch received input event");
}

void MyWatchApp::suspend()
{
    ESP_LOGI(TAG, "WatchApp suspending");
    on_pause();
}

void MyWatchApp::force_close()
{
    ESP_LOGI(TAG, "WatchApp force close");
    on_stop();
    stop_task();
}