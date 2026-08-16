#include "esp_log.h"
#include <stdint.h>
#include <string.h>
#include <string>
#include <memory>
#include <algorithm>
#include <vector>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "os_code/core/window_env/wenv_basicThemes.h"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/applications/menu/app_menu.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"

static const char* TAG = "app_launcher_menu";

// ---------------------------------------------------------------------------

app_launcher_menu::app_launcher_menu(const ApplicationConfig& cfg)
    : AppBase(cfg), selected_index(0), current_menu(nullptr)
{
    appTickRateHZ = 5;
    build_static_menus();
    current_menu = &main_menu;
}

void app_launcher_menu::build_static_menus()
{
    main_menu = {
        {"Watch",        "WatchApp",       false},
        {"Settings",     "SettingsApp",    false},
        {"File Viewer",  "FileViewerApp",  false},
        {"Browser",      "BrowserApp",     false},
        {"Remote",       "RemoteApp",      false},
        {"Wireless",     "WirelessApp",    false},
        {"Health",       "HealthApp",      false},
        {"Games",        "",               true},
        {"Utilities",    "",               true},
        {"Load ELF",     "",               true},
        {"Misc",         "",               true},  // dynamic from registry
        {"Exit",         "WatchApp",       false},
    };

    games_menu = {
        {"2048",   "Game2048App", false},
        {"Pong",   "PongApp",     false},
        {"Snake",  "SnakeApp",    false},
        {"<- Back", "",           true},
    };

    utils_menu = {
        {"Calculator", "CalcApp",      false},
        {"Stopwatch",  "StopwatchApp", false},
        {"Timer",      "TimerApp",     false},
        {"<- Back",    "",             true},
    };

    elf_menu = {
        {"Load test_app", "test_app", false},
        {"<- Back",       "",         true},
    };

    misc_menu = {
        {"<- Back", "", true},
    };
}

bool app_launcher_menu::is_listed_elsewhere(const std::string& app_name) const
{
    if (app_name.empty()) return true;
    auto check = [&](const std::vector<MenuItem>& m) {
        for (const auto& it : m)
            if (!it.is_submenu && it.app_name == app_name) return true;
        return false;
    };
    // Menu itself should never appear as a launch target from misc
    if (app_name == "MenuApp") return true;
    return check(main_menu) || check(games_menu) || check(utils_menu) || check(elf_menu);
}

void app_launcher_menu::rebuild_misc_menu()
{
    misc_menu.clear();

    // Requires appManager::list_registered_apps() (see patch files in artifacts).
    auto apps = appManager::instance().list_registered_apps();
    for (const auto& a : apps) {
        if (is_listed_elsewhere(a.name)) continue;
        MenuItem item;
        item.name = a.display_name.empty() ? a.name : a.display_name;
        item.app_name = a.name;
        item.is_submenu = false;
        misc_menu.push_back(std::move(item));
    }

    if (misc_menu.empty()) {
        misc_menu.push_back(MenuItem{"(no extra apps)", "", false});
    }
    misc_menu.push_back(MenuItem{"<- Back", "", true});

    ESP_LOGI(TAG, "misc_menu rebuilt: %d entries", (int)misc_menu.size());
}

// ---------------------------------------------------------------------------

void app_launcher_menu::on_start()
{
    ESP_LOGI(TAG, "app_launcher_menu started");

    WindowManager::getInstance().SetToolbarActive(false);

    WindowCfg cfg{
        .Posx = 0,
        .Posy = 0,
        .Layer = 0,
        .renderPriority = 0,
        .win_width  = static_cast<uint16_t>((v_env.screen_dim_w - 4)),
        .win_height = static_cast<uint16_t>((v_env.screen_dim_h - 4)),
        .win_rotation = 1,
        .AutoAlignment = false,
        .WrapText = true,
        .borderless = true,
        .ShowNameAtTopOfWindow = false,
        .TextSizeMult = 1,
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

    // Fresh scan every time the menu opens (picks up late-registered apps)
    rebuild_misc_menu();

    selected_index = 0;
    current_menu = &main_menu;
    on_draw();
}

void app_launcher_menu::on_stop()
{
    ESP_LOGI(TAG, "app_launcher_menu stopped");
    WindowManager::getInstance().restore_from_fullscreen();
    if (menu_window) {
        WindowManager::getInstance().unregisterWindow(menu_window);
        menu_window.reset();
    }
    selected_index = 0;
    current_menu = &main_menu;
}

void app_launcher_menu::on_pause()  {}
void app_launcher_menu::on_resume() { rebuild_misc_menu(); on_draw(); }

void app_launcher_menu::on_draw()
{
    if (should_stop_) return;
    if (!menu_window || !current_menu) return;

    std::string menu_text = "<|size=2|><|color=0xFFFF|>";

    if (current_menu == &main_menu)      menu_text += "--- MAIN MENU ---<|n|><|n|>";
    else if (current_menu == &games_menu) menu_text += "--- GAMES ---<|n|><|n|>";
    else if (current_menu == &utils_menu) menu_text += "--- UTILITIES ---<|n|><|n|>";
    else if (current_menu == &elf_menu)   menu_text += "--- LOAD ELF ---<|n|><|n|>";
    else if (current_menu == &misc_menu)  menu_text += "--- MISC (registry) ---<|n|><|n|>";

    int start_idx = 0;
    const int visible_items = 10;
    if ((int)selected_index >= visible_items)
        start_idx = (int)selected_index - visible_items + 1;

    for (int i = start_idx;
         i < (int)current_menu->size() && i < start_idx + visible_items;
         i++) {
        const auto& item = (*current_menu)[i];
        if (i == (int)selected_index) {
            menu_text += "<|color=0xFDFC|>";
            menu_text += item.name;
            if (item.is_submenu) menu_text += " >";
            menu_text += "<|color=0xFFFF|><|n|>";
        } else {
            menu_text += "   ";
            menu_text += item.name;
            if (item.is_submenu) menu_text += " >";
            menu_text += "<|n|>";
        }
    }

    menu_text += "<|n|>";

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    bool show_error = !error_message.empty() && (now - error_timestamp < 2000);

    if (show_error) {
        menu_text += "<|size=1|><|color=0xF800|>";
        menu_text += error_message;
        menu_text += "<|n|>";
    } else {
        menu_text += "<|size=1|><|color=0xFDFC|>";
        menu_text += "^/v=Navigate  ENTER=Select  BACK=Exit";
        if (!error_message.empty()) error_message.clear();
    }

    menu_window->SetText(menu_text);
    menu_window->dirty = true;
}

void app_launcher_menu::tick_app(uint32_t delta_ms)
{
    static uint32_t accumulator = 0;
    accumulator += delta_ms;
    if (accumulator >= 200) {
        on_draw();
        accumulator = 0;
    }
}

bool app_launcher_menu::appmenu_launch_app(uint16_t index)
{
    if (!current_menu) return false;
    if (index >= current_menu->size()) return false;

    const auto& item = (*current_menu)[index];

    if (item.is_submenu) {
        if (item.name == "Games") {
            current_menu = &games_menu;
        } else if (item.name == "Utilities") {
            current_menu = &utils_menu;
        } else if (item.name == "Load ELF") {
            current_menu = &elf_menu;
        } else if (item.name == "Misc") {
            rebuild_misc_menu();          // refresh registry snapshot
            current_menu = &misc_menu;
        } else if (item.name == "<- Back") {
            current_menu = &main_menu;
        }
        selected_index = 0;
        error_message.clear();
        return false;
    }

    if (item.app_name.empty()) return false;

    if (!appManager::instance().is_app_registered(item.app_name)) {
        error_message = "Error: '" + item.app_name + "' not found!";
        error_timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        ESP_LOGW(TAG, "App not registered: %s", item.app_name.c_str());
        return false;
    }

    // MUST copy before close — kill_app destroys this MenuItem (and app_name).
    // Passing a dangling const-ref into open_app → strlen/hash of freed memory.
    std::string target = item.app_name;
    appManager::instance().close_current_and_open(std::move(target));
    return true;
}

void app_launcher_menu::receive_event_input(const void* event)
{
    if (!event || should_stop_ || !current_menu || !menu_window) return;

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
                    return;
                }
                break;
            default:
                break;
        }
    }

    if (needs_redraw && !should_stop_ && menu_window)
        on_draw();
}

void register_menu()
{
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
