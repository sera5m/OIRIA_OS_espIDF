#pragma once
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

//fuck it i'll just throw all the includes and hope it works
class MyWatchApp : public AppBase {
public:
    explicit MyWatchApp(const ApplicationConfig& cfg);

    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void suspend() override;
    void force_close() override;

    // Linux-style lifecycle
    void on_start() override;
    void on_stop() override;
    void on_pause() override;   // optional
    void on_resume() override;  // optional

    // New drawing hook you added
    void on_draw() override;

private:
    std::shared_ptr<Window> main_window_;   // store your watch window here
};
