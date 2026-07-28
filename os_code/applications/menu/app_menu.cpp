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
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "code_stuff/helperfunctions.hpp"
#include "esp_task_wdt.h" 
#include "os_code/applications/menu/app_menu.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/middle_layer/input/hid_t.h"   
#include "os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"
//lol

static const char* TAG = "app_launcher_menu";

// Menu items
struct MenuItem {
    std::string name;
    std::string app_name;
    bool is_submenu;
};

static std::vector<MenuItem> main_menu = {
    {"Watch", "WatchApp", false},
    {"Settings", "SettingsApp", false},
    {"File Viewer", "FileViewerApp", false},
    {"Remote", "RemoteApp", false},
    {"Wireless", "WirelessApp", false},
    {"Health", "HealthApp", false},
    {"Games", "", true},
    {"Utilities", "", true},
    {"Load ELF", "", true},  // Submenu for ELF loading
    {"Exit", "WatchApp", false}
};

static std::vector<MenuItem> games_menu = {
    {"2048", "Game2048App", false},
    {"Pong", "PongApp", false},
    {"Snake", "SnakeApp", false},
    {"<- Back", "", true}
};

static std::vector<MenuItem> utils_menu = {
    {"Calculator", "CalcApp", false},
    {"Stopwatch", "StopwatchApp", false},
    {"Timer", "TimerApp", false},
    {"<- Back", "", true}
};

app_launcher_menu::app_launcher_menu(const ApplicationConfig& cfg)
 : AppBase(cfg), selected_index(0), current_menu(&main_menu)
{
    
    appTickRateHZ = 5;
}

void app_launcher_menu::on_start()
{
        ESP_LOGI(TAG, "app_launcher_menu started – creating window");

    // Disable toolbar + reset positioning fights
    WindowManager::getInstance().SetToolbarActive(false);

    // Disable toolbar + reset positioning fights
    
  //  esp_task_wdt_add(NULL); //add owning task

    WindowCfg cfg{
        .Posx = 0,
        .Posy = 0,
        .Layer = 0,                    // the highest layer is 0
        .renderPriority = 0,
        .win_width = static_cast<uint16_t>((v_env.screen_dim_w-4)),
        .win_height = static_cast<uint16_t>((v_env.screen_dim_h-4)),
        .win_rotation = 1,
        .AutoAlignment = false,
        .WrapText = true,
        .borderless = true,              // better for menu
        .ShowNameAtTopOfWindow = false,
        .TextSizeMult = 1,
        .name = {0},
        .optionsbitmask = 0,
        .BorderColor = 0x12FF,
        .BgColor = 0x0021,
        .Bg_secondaryColor = 0xABCD,
        .WinTextColor = 0xAFFA,
        .backgroundType = BgFillType::Solid,
        .UpdateRate = 1.0f
    };

    menu_window = std::make_shared<Window>(cfg, "menu_window");

    WindowManager::getInstance().registerWindow(menu_window);
    bind_main_window(menu_window);

    WindowManager::getInstance().make_window_fullscreen(menu_window);

    selected_index = 0;
    current_menu = &main_menu;
    on_draw();
}


void app_launcher_menu::on_stop() {
    ESP_LOGI(TAG, "app_launcher_menu stopped");
    WindowManager::getInstance().restore_from_fullscreen();

    if (menu_window) {
        // No need for manual ClearText() anymore — destructor will do it
        WindowManager::getInstance().unregisterWindow(menu_window);
        menu_window.reset();   // This triggers ~Window()
    }

    selected_index = 0;
    current_menu = &main_menu;
}

void app_launcher_menu::on_pause()  { ESP_LOGI(TAG, "app_launcher_menu paused"); }
void app_launcher_menu::on_resume() { ESP_LOGI(TAG, "app_launcher_menu resumed"); }

void app_launcher_menu::on_draw() {
    if (should_stop_) return;
    if (!menu_window) return;
    
    std::string menu_text = "<|size=2|><|color=0xFFFF|>";
    
    if (current_menu == &main_menu) {
        menu_text += "--- MAIN MENU ---<|n|><|n|>";
    } else if (current_menu == &games_menu) {
        menu_text += "--- GAMES ---<|n|><|n|>";
    } else if (current_menu == &utils_menu) {
        menu_text += "--- UTILITIES ---<|n|><|n|>";
    }
    
    int start_idx = 0;
    int visible_items = 10;
    if (selected_index >= visible_items) {
        start_idx = selected_index - visible_items + 1;
    }
    
    for (int i = start_idx; i < (int)current_menu->size() && i < start_idx + visible_items; i++) {
        const auto& item = (*current_menu)[i];
        
        if (i == selected_index) {
            menu_text += "<|color=0xFDFC|>" + item.name + " <|color=0xFFFF|><|n|>";
        } else {
            menu_text += "   " + item.name;
            if (item.is_submenu) menu_text += "→";
            menu_text += "<|n|>";

        }
    }
    
    menu_text += "<|n|>";
    
    // --- Error message handling ---
    static uint32_t error_timestamp = 0;
    static std::string error_message = "";
    
    uint32_t now = esp_timer_get_time() / 1000;
    bool show_error = !error_message.empty() && (now - error_timestamp < 2000);
    
    if (show_error) {
        menu_text += "<|size=2|><|color=0xF000|>" + error_message + "<|n|>";
        // Don't clear immediately - let it display for the full 2 seconds
    } else {
        menu_text += "<|size=1|><|color=0xFDFC|>";
        menu_text += "^/v=Navigate  ENTER=Select  BACK=Exit";
        // Clear error if it expired
        if (!error_message.empty()) {
            error_message = "";
        }
    }
    
    menu_window->SetText(menu_text);
    menu_window->dirty = true;
    menu_window->WinDraw();
    display_framebuffer(true, false);
}

void app_launcher_menu::tick_app(uint32_t delta_ms) {
    static uint32_t accumulator = 0;
    accumulator += delta_ms;

    if (accumulator >= 100) {        // slower for menus
        on_draw();
        accumulator = 0;
    }
}
/*
void app_launcher_menu::force_tick(){
    //if (!is_running_) return;   // safety

    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000);  // current ms
    static uint32_t last_force_tick = 0;
    
    uint32_t delta = current_time - last_force_tick;
    if (delta > 500) delta = 100;   // cap delta to avoid huge jumps

    ESP_LOGD(TAG, "Force tick with delta=%lu ms", delta);
    
    tick_app(delta);
    last_force_tick = current_time;
}*/

void app_launcher_menu::appmenu_launch_app(uint16_t index) {
    if (index >= current_menu->size()) return;

    const auto& item = (*current_menu)[index];

    if (item.is_submenu) {
        if (item.name == "Games") current_menu = &games_menu;
        else if (item.name == "Utilities") current_menu = &utils_menu;
        else if (item.name == "<- Back") current_menu = &main_menu;

        selected_index = 0;
        error_message = "";
    } else if (!item.app_name.empty()) {
        // Check if the app is REGISTERED (exists in manifest), not just running
        if (!appManager::instance().is_app_registered(item.app_name)) {
            // App doesn't exist - show error
            error_message = "Error: '" + item.app_name + "' not found!";
            error_timestamp = esp_timer_get_time() / 1000;
            on_draw();
            ESP_LOGW(TAG, "App not registered: %s", item.app_name.c_str());
            return;
        }
        
        appManager::instance().close_current_and_open(item.app_name);
    }
}

void app_launcher_menu::receive_event_input(const void* event) {
    if (!event) return;

    const InputEvent* ev = static_cast<const InputEvent*>(event);
    bool needs_redraw = false;

    if (ev->action == KeyAction::Tap) {
        switch (ev->key) {
            case KEY_UP:
                selected_index = (selected_index + current_menu->size() - 1) % current_menu->size();
                needs_redraw = true;
                break;

            case KEY_DOWN:
                selected_index = (selected_index + 1) % current_menu->size();
                needs_redraw = true;
                break;

            case KEY_ENTER:
                appmenu_launch_app(selected_index);
                needs_redraw = true;
                break;

            case KEY_BACK:
                if (current_menu != &main_menu) {
                    current_menu = &main_menu;
                    selected_index = 0;
                    needs_redraw = true;
                } else {
                    appManager::instance().close_current_and_open("WatchApp");
                }
                break;
        }
    }

    if (needs_redraw) {
        on_draw();
    }
}




// ========================================================
// Registration
// ========================================================

static std::shared_ptr<AppBase> create_menu(const ApplicationConfig& cfg) {
    return std::make_shared<app_launcher_menu>(cfg);
}

// Registration function
void register_menu() {
    AppManifest m;
    m.name = "MenuApp";
    m.display_name = "Menu";
    m.description = "Application launcher";
    m.capabilities = static_cast<uint32_t>(AppCapability::FULLSCREEN) | 
                     static_cast<uint32_t>(AppCapability::NEEDS_WINDOW);
    m.stack_size_bytes = 8192;
    m.priority = 5;
    m.tick_rate_hz = 5;
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<app_launcher_menu>(cfg);
    };
    
    appManager::instance().register_app(m);
}