#include "MS_watchapp.hpp"
#include "esp_log.h"
#include <stdint.h>
#include "esp_timer.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include <string>
#include <memory>
#include <sstream>
#include <algorithm>
#include <variant>//unions for the code
#include "code_stuff/types.h"
#include <memory>
#include <math.h>
#include "hardware/wiring/wiring.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "rom/cache.h"
#include <string.h>
#include <math.h>
#include "hardware/drivers/abstraction_layers/al_scr.h"

#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "os_code\core\window_env\wenv_basicThemes.h"
#include <vector>

#include "../../../hardware/drivers/psram_std/psram_std.hpp" //my custom work for psram stdd things
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
#include <memory>
#include "os_code/core/rShell/enviroment/env_vars.h"

#include "os_code/core/rShell/s_hell.hpp"   // app framework
#include "os_code/core/rShell/enviroment/env_vars.h"


#include "os_code/core/window_env/MWenv.hpp"


static const char* TAG = "MyWatchApp";

MyWatchApp::MyWatchApp(const ApplicationConfig& cfg)
    : AppBase(cfg)
{
    // Optional: set some watch-specific capabilities
    // cfg_ is already copied in base class, but you can tweak if needed
}

void MyWatchApp::on_start()
{
    ESP_LOGI(TAG, "WatchApp started – creating window");

    // === Create the watch window (exactly as you showed) ===
    watch_window = std::make_shared<Window>(
        WindowCfg{
            .Posx = ((v_env.screen_dim_w)/4),
            .Posy = 40,
            .Layer = 0,
            .renderPriority = 0,
            .win_width = ((v_env.screen_dim_w)/2),
            .win_height = ((v_env.screen_dim_H)/2),
            .win_rotation = 1,
            .AutoAlignment = false,
            .WrapText = true,
            .borderless = false,
            .ShowNameAtTopOfWindow = false,
            .TextSizeMult = 1,
            .name = {0},                    // empty name
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

    // If your framework uses window_ from AppBase, you can also do:
    // window_ = watch_window;

    // Show it (if your Window class has a show() method)
    // watch_window->show();

    // You can also call on_draw() once here if you want initial draw
    on_draw();
}

void MyWatchApp::on_stop()
{
    ESP_LOGI(TAG, "WatchApp stopped – cleaning up window");
    watch_window.reset();   // release the window
}

void MyWatchApp::on_pause()
{
    ESP_LOGI(TAG, "WatchApp paused");
    // e.g. stop animations, sensors, etc.
}

void MyWatchApp::on_resume()
{
    ESP_LOGI(TAG, "WatchApp resumed");
    // restart things if needed
}

void MyWatchApp::on_draw()
{
    if (!watch_window) return;

    auto& win = *watch_window;

    win.calculateLogicalDimensions();

    int cx = win.logicalW / 2;
    int cy = win.logicalH / 2;

    std::stringstream ss;

    ss << "<|pos=" << cx << "," << cy << "|>"
       << "<|size=3|>"
       << tostr(v_env.displayTime.hh) << ":"
       << tostr(v_env.displayTime.mm)
       << ":<|size=1|>"
       << tostr(v_env.displayTime.ss);

    win.SetText(ss.str());

    ESP_LOGD(TAG, "Watch frame drawn");
}

void MyWatchApp::tick_app(uint32_t delta_ms)
{
    // This runs at ~20 Hz (or whatever period you set in run())

    // 1. Update internal state (time, sensors, animations...)
    // update_time();
    // read_sensors();

    // 2. Redraw only when needed (or every N ticks to save power)
    static uint32_t frame_counter = 0;
    frame_counter += delta_ms;

    if (frame_counter >= 1000) {        // e.g. redraw every second for a watch
        on_draw();
        frame_counter = 0;
    }

    // Or redraw every tick if you want smooth animations:
    // on_draw();
}

void MyWatchApp::receive_event_input(const void* event)
{
    // Handle touch / buttons here
    // Example:
    // const TouchEvent* te = static_cast<const TouchEvent*>(event);
    // if (te && te->type == TAP) { ... }

    ESP_LOGI(TAG, "Watch received input event");
}

void MyWatchApp::suspend()
{
    ESP_LOGI(TAG, "WatchApp suspending");
    on_pause();
    // You can also stop_task() here if you want to fully pause the task
}

void MyWatchApp::force_close()
{
    ESP_LOGI(TAG, "WatchApp force close");
    on_stop();
    stop_task();
}