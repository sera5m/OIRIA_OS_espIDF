#pragma once
#include <stdint.h>
#include "esp_timer.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include <string>
#include <memory>
#include <sstream>
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
#include "esp_log.h"
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



typedef enum{WM_MAIN,
     WM_STOPWATCH,
     WM_ALARMS,
     WM_TIMER,
     WM_NTP_SYNCH,
      WM_SET_TIME,
     WM_SET_TIMEZONE,
     WM_COUNT
    }WatchMode;
//back in 2025ish when i was originally working on this i had a mode thing for a menu,it was messy so i'll just add a menu app instead of this nonsense

// Helper function to convert numbers to string with 2-digit formatting
inline std::string tostr(int value) {
    char buffer[3];
    snprintf(buffer, sizeof(buffer), "%02d", value);
    return std::string(buffer);
}

extern char time_str[];

class MyWatchApp : public AppBase {
public:
    explicit MyWatchApp(const ApplicationConfig& cfg);

    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void suspend() override;
    void force_close() override;

    void on_start() override;
    void on_stop() override;
    void on_pause() override;
    void on_resume() override;
    void on_draw() override;

private:
    std::shared_ptr<Window> watch_window;
};