#include "defaultAppList.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "esp_log.h"

static const char* TAG = "AppRegistry";

AppRegistry& AppRegistry::instance() {
    static AppRegistry registry;
    return registry;
}

void AppRegistry::register_builtin_app(const AppInfo& info) {
    builtin_apps_.push_back(info);
    appManager::instance().register_app_type(info.name, info.creator);
    ESP_LOGI(TAG, "Registered builtin app: %s", info.name.c_str());
}

std::vector<AppInfo> AppRegistry::getAllApps() const {
    std::vector<AppInfo> all = builtin_apps_;
    all.insert(all.end(), dynamic_apps_.begin(), dynamic_apps_.end());
    return all;
}

const AppInfo* AppRegistry::findApp(const std::string& name) const {
    for (const auto& app : builtin_apps_) {
        if (app.name == name) return &app;
    }
    for (const auto& app : dynamic_apps_) {
        if (app.name == name) return &app;
    }
    return nullptr;
}

std::shared_ptr<AppBase> AppRegistry::open_app(const std::string& name) {
    return appManager::instance().open_app(name);
}

size_t AppRegistry::getAppCount() const {
    return builtin_apps_.size() + dynamic_apps_.size();
}

void AppRegistry::scan_dynamic_apps() {
    ESP_LOGW(TAG, "Dynamic app scanning (ELF) not implemented yet - see rshell_elif_link");
}
