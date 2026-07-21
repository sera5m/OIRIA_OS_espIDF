#include "os_code/core/rShell/rshell_elif_link.hpp"
#include "esp_log.h"

static const char* TAG = "ElifLoader";

std::shared_ptr<AppBase> RshellElifLoader::load_from_sd(const std::string& path) {
    // TODO: ELF load, symbol lookup, instantiate
    auto app = appManager::instance().create_dynamic_app("LoadedApp");
    return app;
}

bool RshellElifLoader::unload_app(const std::string& name) {
    ESP_LOGI(TAG, "Unloading dynamic app: %s", name.c_str());
    return false;
}
