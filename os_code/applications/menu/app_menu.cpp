
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

// Menu items - can be moved to config later
struct MenuItem {
    std::string name;
    std::string app_name;  // The registered app name to launch
    bool is_submenu;       // If true, opens submenu instead of app
};

static std::vector<MenuItem> main_menu = {
    {"Watch", "WatchApp", false},
    {"Settings", "SettingsApp", false},
    {"File Viewer", "FileViewerApp", false},
    {"Remote", "RemoteApp", false},
    {"Wireless", "WirelessApp", false},
    {"Health", "HealthApp", false},
    {"Games", "", true},      // Submenu
    {"Utilities", "", true},  // Submenu
    {"Exit", "WatchApp", false}  // Exit back to watch
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



// ===================================================================
// Fast helpers (no snprintf)
// ===================================================================


 // ===================================================================

 app_launcher_menu::app_launcher_menu(const ApplicationConfig& cfg)
 : AppBase(cfg), selected_index(0), current_menu(&main_menu)
{
 appTickRateHZ = 5;
}

void app_launcher_menu::on_start()
{
    ESP_LOGI(TAG, "app_launcher_menu started – creating window");

    menu_window = std::make_shared<Window>(
        WindowCfg{
            .Posx = static_cast<uint16_t>(0),
            .Posy = static_cast<uint16_t>(0),
            .Layer = 0,
            .renderPriority = 0,
            .win_width = static_cast<uint16_t>(v_env.clamped_screen_dim_w),
            .win_height = static_cast<uint16_t>((v_env.clamped_screen_dim_h)),
            .win_rotation = 1,
            .AutoAlignment = false,
            .WrapText = true,
            .borderless = false,
            .ShowNameAtTopOfWindow = false,
            .TextSizeMult = 1,
            .name = {0},
            .optionsbitmask = 0,
            .BorderColor = 0x12FF,
            .BgColor = 0xB00B,
            .Bg_secondaryColor = 0xABCD,
            .WinTextColor = 0xAFFA,
            .backgroundType = BgFillType::Solid,
            .UpdateRate = 1.0f
        },
        "time"
    );
    
    WindowManager::getInstance().registerWindow(menu_window);
    bind_main_window(menu_window);
    ESP_LOGI(TAG, "app_launcher_menu Window registered");
    on_draw();  // initial draw
}

void app_launcher_menu::on_stop()
{
    ESP_LOGI(TAG, "app_launcher_menu stopped");
    if (menu_window) menu_window.reset();
}

void app_launcher_menu::on_pause()  { ESP_LOGI(TAG, "app_launcher_menu paused"); }
void app_launcher_menu::on_resume() { ESP_LOGI(TAG, "app_launcher_menu resumed"); }

void app_launcher_menu::on_draw() {
    if (!menu_window) return;
    
    std::string menu_text = "<|size=2|><|color=0xFFFF|>";
    
    // Draw title based on current menu
    if (current_menu == &main_menu) {
        menu_text += "=== MAIN MENU ===<|n|><|n|>";
    } else if (current_menu == &games_menu) {
        menu_text += "=== GAMES ===<|n|><|n|>";
    } else if (current_menu == &utils_menu) {
        menu_text += "=== UTILITIES ===<|n|><|n|>";
    }
    
    // Draw menu items with highlighting
    int start_idx = 0;
    int visible_items = 10;  // Show up to 10 items
    
    if (selected_index >= visible_items) {
        start_idx = selected_index - visible_items + 1;
    }
    
    for (int i = start_idx; i < current_menu->size() && i < start_idx + visible_items; i++) {
        const auto& item = (*current_menu)[i];
        
        if (i == selected_index) {
            // Highlight selected item
            menu_text += "<|color=0x0000|><|hl=0xFFFF|> ▶ ";
            menu_text += item.name;
            menu_text += " <|/hl|><|n|>";
        } else {
            // Normal item
            menu_text += "   ";
            menu_text += item.name;
            if (item.is_submenu) {
                menu_text += " →";  // Indicator for submenu
            }
            menu_text += "<|n|>";
        }
    }
    
    // Add navigation hints at bottom
    menu_text += "<|n|><|size=1|><|color=0x8888|>";
    menu_text += "▲/▼=Navigate  ENTER=Select  BACK=Exit";
    
    menu_window->SetText(menu_text);
    menu_window->dirty = true;
    menu_window->WinDraw();
    display_framebuffer(true, false);

    }


void app_launcher_menu::tick_app(uint32_t delta_ms)
{
    static uint32_t accumulator = 0;
    accumulator += delta_ms;

    if (accumulator >= 100) {
        // Redraw only if needed (when dirty)
        if (menu_window && menu_window->dirty) {
            on_draw();
        }
        accumulator = 0;
    }
}

void app_launcher_menu::receive_event_input(const void* event)
{
    if (!event) return;
    
    const InputEvent* ev = static_cast<const InputEvent*>(event);
    
    if (ev->action == KeyAction::Tap) {
        switch (ev->key) {
            case KEY_UP:
                selected_index--;
                if (selected_index < 0) selected_index = current_menu->size() - 1;
                menu_window->dirty = true;
                ESP_LOGI(TAG, "Menu: UP - index=%d", selected_index);
                break;
                
            case KEY_DOWN:
                selected_index++;
                if (selected_index >= current_menu->size()) selected_index = 0;
                menu_window->dirty = true;
                ESP_LOGI(TAG, "Menu: DOWN - index=%d", selected_index);
                break;
                
            case KEY_ENTER: {
                if (selected_index >= 0 && selected_index < current_menu->size()) {
                    const auto& item = (*current_menu)[selected_index];
                    ESP_LOGI(TAG, "Selected: %s (submenu=%d)", item.name.c_str(), item.is_submenu);
                    
                    if (item.is_submenu) {
                        // Handle submenu navigation
                        if (item.name == "Games") {
                            current_menu = &games_menu;
                            selected_index = 0;
                            menu_window->dirty = true;
                        } else if (item.name == "Utilities") {
                            current_menu = &utils_menu;
                            selected_index = 0;
                            menu_window->dirty = true;
                        } else if (item.name == "<- Back") {
                            // Go back to main menu
                            current_menu = &main_menu;
                            selected_index = 0;
                            menu_window->dirty = true;
                        }
                    } else {
                        // Launch the app
                        ESP_LOGI(TAG, "Launching app: %s", item.app_name.c_str());
                        auto& registry = AppRegistry::instance();
                        registry.open_app(item.app_name);
                    }
                }
                break;
            }
                
            case KEY_BACK:
                ESP_LOGI(TAG, "BACK pressed - returning to watch");
                AppRegistry::instance().open_app("WatchApp");
                break;
        }
    }
}

void app_launcher_menu::suspend()
{
    ESP_LOGI(TAG, "menu suspending");
    on_pause();
}

void app_launcher_menu::force_close()
{
    ESP_LOGI(TAG, " force close");
    on_stop();
    stop_task();
}



// Register MenuApp
REGISTER_BUILTIN_APP(app_launcher_menu, "MenuApp", "App Launcher", "Launch other apps",
    static_cast<uint32_t>(AppCapability::FULLSCREEN) | 
    static_cast<uint32_t>(AppCapability::NEEDS_WINDOW),
    8192, 5, 10);