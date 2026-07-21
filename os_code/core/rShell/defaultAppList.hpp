/*
 * defaultAppList.h
 *
 *  Created on: May 22, 2026
 *      Author: ash
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
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "code_stuff/helperfunctions.hpp"
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
//hmm, this is data only. sorry i'm including everyingh here lol


struct AppInfo {
    std::string name;
    std::string display_name;
    std::string description;
    AppCapabilities capabilities;
    size_t stack_size;
    UBaseType_t priority;
    int tick_rate_hz;
    bool is_system_app = true;
    bool is_dynamic = false;

    using AppCreator = std::function<std::shared_ptr<AppBase>()>;
    AppCreator creator;
};

class AppRegistry {
public:
    static AppRegistry& instance();

    void register_builtin_app(const AppInfo& info);

    void scan_dynamic_apps();  // TODO: ELF loading later

    std::vector<AppInfo> getAllApps() const;

    const AppInfo* findApp(const std::string& name) const;

    // Improved open
    std::shared_ptr<AppBase> open_app(const std::string& name);

    size_t getAppCount() const;

private:
    AppRegistry() = default;

    std::vector<AppInfo> builtin_apps_;
    std::vector<AppInfo> dynamic_apps_;
};


#define REGISTER_BUILTIN_APP(CLASS, APP_NAME, DISPLAY_NAME, DESC, CAPS, STACK, PRIO, TICK_RATE) \
namespace { \
    struct Register##CLASS { \
        Register##CLASS() { \
            AppInfo info; \
            info.name = APP_NAME; \
            info.display_name = DISPLAY_NAME; \
            info.description = DESC; \
            info.capabilities = static_cast<AppCapabilities>(CAPS); \
            info.stack_size = STACK; \
            info.priority = PRIO; \
            info.tick_rate_hz = TICK_RATE; \
            info.creator = CLASS::create_instance; \
            AppRegistry::instance().register_builtin_app(info); \
        } \
    }; \
    static Register##CLASS reg_##CLASS; \
}

#endif
