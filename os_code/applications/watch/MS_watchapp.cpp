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
WatchMode CurrentWatchMode=WatchMode::WM_MAIN;
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

char time_str[128];
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
            .Posy = static_cast<uint16_t>(0),
            .Layer = 0,
            .renderPriority = 0,
            .win_width = static_cast<uint16_t>(v_env.clamped_screen_dim_w),
            .win_height = static_cast<uint16_t>((v_env.clamped_screen_dim_h / 2)),
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
    bind_main_window(watch_window);
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
    
    if (current_second != last_second) {
        int month_idx = v_env.displayTime.month - 1;
        if (month_idx < 0) month_idx = 0;
        if (month_idx > 11) month_idx = 11;
        
        snprintf(time_str, sizeof(time_str), 
                 "<|size=5|><|color=0xAD0F|>%02d:%02d<|size=2|>:%02d<|n|><|n|><|size=2|>%s %d, %d", 
                 v_env.displayTime.hh, 
                 v_env.displayTime.mm, 
                 v_env.displayTime.ss,
                 months[month_idx],
                 v_env.displayTime.day,
                 v_env.displayTime.year);
        
        ESP_LOGI(TAG, "Time string: %s", time_str);  // Debug
        
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











void MyWatchApp::watchapp_back(){  // This is a FREE function (not a class member)
    if (CurrentWatchMode == WM_MAIN) {
        appManager::instance().close_current_and_open("MenuApp");
    } else {
        CurrentWatchMode = WM_MAIN;
        on_draw();  
    }
}


void MyWatchApp::receive_event_input(const void* event)
{
    if (!event) return;
    
    const InputEvent* ev = static_cast<const InputEvent*>(event);
    
    ESP_LOGI(TAG, "Watch received input: key=0x%04X, action=%d", ev->key, (int)ev->action);
    
    switch (ev->action) {
        case KeyAction::Tap:
            ESP_LOGI(TAG, "Key tap: 0x%04X", ev->key);
            switch (ev->key) {
                case KEY_UP:
                    ESP_LOGI(TAG, "UP pressed");
                    break;
                case KEY_DOWN:
                    ESP_LOGI(TAG, "DOWN pressed");
                    break;
                case KEY_LEFT:
                    ESP_LOGI(TAG, "LEFT pressed");
                    break;
                case KEY_RIGHT:
                    ESP_LOGI(TAG, "RIGHT pressed");
                    break;
                case KEY_ENTER:
                    ESP_LOGI(TAG, "ENTER pressed - switch to stopwatch?");
                    CurrentWatchMode = WM_STOPWATCH;
                    on_draw();
                    break;
                case KEY_BACK:
                    ESP_LOGI(TAG, "BACK pressed");
                    watchapp_back();
                    break;
            }
            break;
            
        case KeyAction::PositionDelta:
            ESP_LOGI(TAG, "Knob delta: %+d", ev->delta);
            // Use knob for something - maybe brightness, volume, or scrolling
            if (ev->delta > 0) {
                // Rotated clockwise
            } else if (ev->delta < 0) {
                // Rotated counter-clockwise
            }
            break;
            
        case KeyAction::Hold:
            ESP_LOGI(TAG, "Key hold: 0x%04X", ev->key);
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown action: %d", (int)ev->action);
            break;
    }
}


//appManager::swap_task

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