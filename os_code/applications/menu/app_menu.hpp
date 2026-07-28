#pragma once
#include <stdint.h>
#include <string>
#include <memory>
#include <vector>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "os_code/core/window_env/wenv_basicThemes.h"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/core/rShell/rshell_appFramework.hpp"
// Forward declaration

// Forward declarations
struct InputEvent;
struct MenuItem;


class app_launcher_menu : public AppBase {
public:
    explicit app_launcher_menu(const ApplicationConfig& cfg);

    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void on_draw() override;

    void on_start() override;
    void on_stop() override;
    void on_pause() override;
    void on_resume() override;
    void appmenu_launch_app(uint16_t index);


private:
    std::shared_ptr<Window> menu_window;
    uint16_t selected_index = 0;
    std::vector<MenuItem>* current_menu;
    
    // Error message handling
    std::string error_message;
    uint32_t error_timestamp = 0; 
};

// Registration function - call from main
void register_menu();