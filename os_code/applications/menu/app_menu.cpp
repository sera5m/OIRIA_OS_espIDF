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

static std::vector<MenuItem> elf_menu = {
    {"Load test_app", "test_app", false},
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
    if (!current_menu) return;

    std::string menu_text = "<|size=2|><|color=0xFFFF|>";

    if (current_menu == &main_menu) {
        menu_text += "--- MAIN MENU ---<|n|><|n|>";
    } else if (current_menu == &games_menu) {
        menu_text += "--- GAMES ---<|n|><|n|>";
    } else if (current_menu == &utils_menu) {
        menu_text += "--- UTILITIES ---<|n|><|n|>";
    } else if (current_menu == &elf_menu) {
        menu_text += "--- LOAD ELF ---<|n|><|n|>";
    }

    int start_idx = 0;
    const int visible_items = 10;
    if ((int)selected_index >= visible_items) {
        start_idx = (int)selected_index - visible_items + 1;
    }

    for (int i = start_idx;
         i < (int)current_menu->size() && i < start_idx + visible_items;
         i++) {
        const auto& item = (*current_menu)[i];

        if (i == (int)selected_index) {
            menu_text += "<|color=0xFDFC|>" + item.name + " <|color=0xFFFF|><|n|>";
        } else {
            menu_text += "   " + item.name;
            if (item.is_submenu) menu_text += ">";
            menu_text += "<|n|>";
        }
    }

    menu_text += "<|n|>";

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    bool show_error = !error_message.empty() && (now - error_timestamp < 2000);

    if (show_error) {
        menu_text += "<|size=2|><|color=0xF000|>" + error_message + "<|n|>";
    } else {
        menu_text += "<|size=1|><|color=0xFDFC|>";
        menu_text += "^/v=Navigate  ENTER=Select  BACK=Exit";
        if (!error_message.empty()) error_message.clear();
    }

    menu_window->SetText(menu_text);
    menu_window->dirty = true;
    // Prefer letting WindowManager push – direct display_framebuffer from the
    // menu task can race the display core. Keep if you need it for now:
    // menu_window->WinDraw();
    // display_framebuffer(true, false);
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

bool app_launcher_menu::appmenu_launch_app(uint16_t index) {
    if (!current_menu) return false;
    if (index >= current_menu->size()) return false;

    const auto& item = (*current_menu)[index];

    if (item.is_submenu) {
        if (item.name == "Games")           current_menu = &games_menu;
        else if (item.name == "Utilities")  current_menu = &utils_menu;
        else if (item.name == "Load ELF")   current_menu = &elf_menu;
        else if (item.name == "<- Back")    current_menu = &main_menu;

        selected_index = 0;
        error_message.clear();
        return false;   // still in menu
    }

    if (item.app_name.empty()) return false;

    if (!appManager::instance().is_app_registered(item.app_name)) {
        error_message = "Error: '" + item.app_name + "' not found!";
        error_timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        ESP_LOGW(TAG, "App not registered: %s", item.app_name.c_str());
        return false;
    }

    // LEAVING the menu – do not touch this object after this call
    appManager::instance().close_current_and_open(item.app_name);
    return true;   // launched → caller must return immediately
}

void app_launcher_menu::receive_event_input(const void* event) {
    if (!event) return;
    // App is shutting down / already replaced – ignore input
    if (should_stop_) return;
    if (!current_menu) return;
    if (!menu_window) return;

    const InputEvent* ev = static_cast<const InputEvent*>(event);
    bool needs_redraw = false;

    if (ev->action == KeyAction::Tap) {
        switch (ev->key) {
            case KEY_UP:
                if (!current_menu->empty()) {
                    selected_index = (selected_index + (int)current_menu->size() - 1)
                                     % (int)current_menu->size();
                    needs_redraw = true;
                }
                break;

            case KEY_DOWN:
                if (!current_menu->empty()) {
                    selected_index = (selected_index + 1) % (int)current_menu->size();
                    needs_redraw = true;
                }
                break;

            case KEY_ENTER: {
                // If we launched another app, this instance may be destroyed
                // immediately – do not redraw or touch members after this.
                bool left = appmenu_launch_app(selected_index);
                if (left) return;
                needs_redraw = true;
                break;
            }

            case KEY_BACK:
                if (current_menu != &main_menu) {
                    current_menu = &main_menu;
                    selected_index = 0;
                    needs_redraw = true;
                } else {
                    appManager::instance().close_current_and_open("WatchApp");
                    return;   // same rule – object may be gone
                }
                break;

            default:
                break;
        }
    }

    if (needs_redraw && !should_stop_ && menu_window) {
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