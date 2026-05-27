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
#include "os_code/core/rShell/defaultAppList.hpp" 


struct MenuItem;

class app_launcher_menu : public AppBase {
public:
    explicit app_launcher_menu(const ApplicationConfig& cfg);

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
    std::shared_ptr<Window> menu_window;
    int selected_index = 0;
    std::vector<MenuItem>* current_menu;  // Pointer to active menu
};

static ApplicationConfig make_menu_config() {
    ApplicationConfig cfg;
    cfg.capabilities = static_cast<uint32_t>(AppCapability::FULLSCREEN) |
                       static_cast<uint32_t>(AppCapability::NEEDS_WINDOW);
    cfg.stack_size_bytes = 8192;
    cfg.priority = 5;
    cfg.name = "MenuApp";
    cfg.tick_rate_hz = 10;
    return cfg;
}