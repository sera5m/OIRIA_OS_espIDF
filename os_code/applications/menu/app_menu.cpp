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

#include "os_code/applications/menu/app_menu.hpp"
#include "os_code/core/rShell/defaultAppList.hpp" 

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
    
    for (int i = start_idx; i < current_menu->size() && i < start_idx + visible_items; i++) {
        const auto& item = (*current_menu)[i];
        
        if (i == selected_index) {
            menu_text += "<|color=0xFDFC|>" + item.name + " <|color=0xFFFF|><|n|>";
        } else {
            menu_text += "   " + item.name;
            if (item.is_submenu) menu_text += "-";
            menu_text += "<|n|>";
        }
    }
    
    menu_text += "<|n|><|size=1|><|color=0xFDFC|>";
    menu_text += "^/v=Navigate  ENTER=Select  BACK=Exit";
    
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

void app_launcher_menu::force_tick(){
    //if (!is_running_) return;   // safety

    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000);  // current ms
    static uint32_t last_force_tick = 0;
    
    uint32_t delta = current_time - last_force_tick;
    if (delta > 500) delta = 100;   // cap delta to avoid huge jumps

    ESP_LOGD(TAG, "Force tick with delta=%lu ms", delta);
    
    tick_app(delta);
    last_force_tick = current_time;
}

void app_launcher_menu::receive_event_input(const void* event)
{
    if (!event) return;
    
    const InputEvent* ev = static_cast<const InputEvent*>(event);
    
    ESP_LOGI(TAG, "Menu received: key=0x%04X action=%d", ev->key, (int)ev->action);

    bool needs_redraw = false;

    if (ev->action == KeyAction::Tap) {
        switch (ev->key) {
            case KEY_UP:
                selected_index = (selected_index - 1 + current_menu->size()) % current_menu->size();
                needs_redraw = true;
                break;
                
            case KEY_DOWN:
                selected_index = (selected_index + 1) % current_menu->size();
                needs_redraw = true;
                break;
                
            case KEY_ENTER: {
                if (selected_index < 0 || selected_index >= current_menu->size()) break;
                
                const auto& item = (*current_menu)[selected_index];
                
                if (item.is_submenu) {
                    if (item.name == "Games") current_menu = &games_menu;
                    else if (item.name == "Utilities") current_menu = &utils_menu;
                    else if (item.name == "<- Back") current_menu = &main_menu;
                    
                    selected_index = 0;
                    needs_redraw = true;
                } else if (!item.app_name.empty()) {
                    force_close();
                    AppRegistry::instance().open_app(item.app_name);
                    return;
                }
                break;
            }
                
            case KEY_BACK:
                force_close();
                AppRegistry::instance().open_app("WatchApp");
                return;
        }
    }

    if (needs_redraw) {
        menu_window->dirty = true;
        app_launcher_menu::force_tick();          // ← This forces immediate tick + redraw
    }
}

void app_launcher_menu::suspend()  { ESP_LOGI(TAG, "menu suspending"); }

void app_launcher_menu::force_close()
{
    ESP_LOGI(TAG, "force close");
    on_stop();
    stop_task();
}
// Register
REGISTER_BUILTIN_APP(app_launcher_menu, "MenuApp", "App Launcher", "Launch other apps",
    static_cast<uint32_t>(AppCapability::FULLSCREEN) | 
    static_cast<uint32_t>(AppCapability::NEEDS_WINDOW),
    8192, 5, 10);