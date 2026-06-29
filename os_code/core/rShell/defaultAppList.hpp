/*
 * defaultAppList.h
 *
 *  Created on: May 22, 2026
 *      Author: dev
 */
#ifndef MAIN_OS_CODE_CORE_RSHELL_DEFAULTAPPLIST_HPP_
#define MAIN_OS_CODE_CORE_RSHELL_DEFAULTAPPLIST_HPP_


#include <string>
#include <vector>
#include <memory>


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
#include "os_code/core/window_env/wenv_basicThemes.h"
#include "../../../hardware/drivers/psram_std/psram_std.hpp"
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/core/rShell/s_hell.hpp"
#include "code_stuff/helperfunctions.hpp"
//hmm, this is data only. sorry i'm including everyingh here lol



struct AppInfo {
    std::string name;
    std::string display_name;  // For menu display
    std::string description;
    AppCapabilities capabilities;
    size_t stack_size;
    UBaseType_t priority;
    int tick_rate_hz;
    bool is_system_app;  // Can't be uninstalled
    bool is_dynamic;     // Loaded from SD card vs built-in
    
    // Factory function to create the app
    using AppCreator = std::function<std::shared_ptr<AppBase>()>;
    AppCreator creator;
};

class AppRegistry {
public:
    static AppRegistry& instance() {
        static AppRegistry instance;
        return instance;
    }
    
    // Register a built-in app
    void register_builtin_app(const AppInfo& info) {
        builtin_apps_.push_back(info);
        // Also register with appManager's factory
        appManager::instance().register_app_type(info.name, info.creator);
    }
    
    // Load dynamic apps from SD card (to be implemented later because i have no idea how the fuck)
    void scan_dynamic_apps() {
        // TODO: Scan /apps/ directory on SD card
        // Load .elf or .bin files
    }
    
    // Get all apps (built-in + dynamic)
    std::vector<AppInfo> getAllApps() const {
        std::vector<AppInfo> all = builtin_apps_;
        all.insert(all.end(), dynamic_apps_.begin(), dynamic_apps_.end());
        return all;
    }
    
    // Find app by name
    const AppInfo* findApp(const std::string& name) const {
        for (const auto& app : builtin_apps_) {
            if (app.name == name) return &app;
        }
        for (const auto& app : dynamic_apps_) {
            if (app.name == name) return &app;
        }
        return nullptr;
    }
    
    
    // Open/launch an app by name
    bool open_app(const std::string& name) {
        auto app = appManager::instance().get_app(name);
        if (!app) {
            app = appManager::instance().launch_app(name);
        } else {
            appManager::instance().swap_to_app(app);
        }
        return app != nullptr;
    }
    
    // Get app count
    size_t getAppCount() const { return builtin_apps_.size() + dynamic_apps_.size(); }
    
    // Store in PSRAM (usigng PSRAM allocator for large lists)
    void allocateInPSRAM() {
        // Move vectors to PSRAM if needed
        // This depends on your PSRAM allocator implementation
    }
    
private:
    AppRegistry() = default;
    
    std::vector<AppInfo> builtin_apps_;   // Built-in apps
    std::vector<AppInfo> dynamic_apps_;   // Loaded from SD card (stored in PSRAM)
};

// Helper macro to register apps easily
#define REGISTER_BUILTIN_APP(CLASS, APP_NAME, DISPLAY_NAME, DESC, CAPS, STACK, PRIO, TICK_RATE) \
    namespace { \
        struct Register##CLASS { \
            Register##CLASS() { \
                AppInfo info; \
                info.name = APP_NAME; \
                info.display_name = DISPLAY_NAME; \
                info.description = DESC; \
                info.capabilities = CAPS; \
                info.stack_size = STACK; \
                info.priority = PRIO; \
                info.tick_rate_hz = TICK_RATE; \
                info.is_system_app = true; \
                info.is_dynamic = false; \
                info.creator = []() { \
                    ApplicationConfig cfg; \
                    cfg.capabilities = CAPS; \
                    cfg.stack_size_bytes = STACK; \
                    cfg.priority = PRIO; \
                    cfg.name = APP_NAME; \
                    cfg.tick_rate_hz = TICK_RATE; \
                    return std::make_shared<CLASS>(cfg); \
                }; \
                AppRegistry::instance().register_builtin_app(info); \
            } \
        }; \
        static Register##CLASS reg_##CLASS; \
    }





#endif /* MAIN_OS_CODE_CORE_RSHELL_DEFAULTAPPLIST_HPP_ */
