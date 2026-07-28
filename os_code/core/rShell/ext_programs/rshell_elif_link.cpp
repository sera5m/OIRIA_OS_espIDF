#include "os_code/core/rShell/rshell_elif_link.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_elf.h"
#include "esp_elf_adapter.h"
#include "esp_elf_symbol.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static const char* TAG = "ElifLoader";

// ================================================================
// Singleton
// ================================================================

RshellElifLoader& RshellElifLoader::instance() {
    static RshellElifLoader inst;
    return inst;
}

// ================================================================
// Main Load Function
// ================================================================

std::shared_ptr<AppBase> RshellElifLoader::load_from_sd(const std::string& path) {
    ESP_LOGI(TAG, "Loading ELF app from: %s", path.c_str());
    
    // Check if already loaded
    for (const auto& [name, loaded] : loaded_apps) {
        if (loaded.elf_path == path) {
            ESP_LOGW(TAG, "App already loaded: %s", name.c_str());
            return loaded.app;
        }
    }
    
    // 1. Load the ELF file
    void* elf_handle = nullptr;
    size_t elf_size = 0;
    if (!load_elf_file(path, &elf_handle, &elf_size)) {
        ESP_LOGE(TAG, "Failed to load ELF: %s", path.c_str());
        return nullptr;
    }
    
    // 2. Find the app name (try to get from ELF or use filename)
    std::string app_name = path;
    size_t last_slash = app_name.find_last_of('/');
    if (last_slash != std::string::npos) {
        app_name = app_name.substr(last_slash + 1);
    }
    size_t dot = app_name.find_last_of('.');
    if (dot != std::string::npos) {
        app_name = app_name.substr(0, dot);
    }
    
    // 3. Create app from ELF
    std::shared_ptr<AppBase> app;
    if (!create_app_from_elf(elf_handle, app_name, app)) {
        ESP_LOGE(TAG, "Failed to create app from ELF");
        // Clean up ELF
        esp_elf_unload(elf_handle);
        return nullptr;
    }
    
    // 4. Store loaded app info
    LoadedApp loaded;
    loaded.name = app_name;
    loaded.app = app;
    loaded.elf_handle = elf_handle;
    loaded.elf_path = path;
    loaded.memory_used = elf_size;
    
    loaded_apps[app_name] = loaded;
    
    ESP_LOGI(TAG, "Successfully loaded ELF app: %s (size: %zu bytes)", 
             app_name.c_str(), elf_size);
    
    return app;
}

// ================================================================
// Load ELF File
// ================================================================

bool RshellElifLoader::load_elf_file(const std::string& path, void** out_handle, size_t* out_size) {
    // Open the ELF file from SD card
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to open ELF file: %s", path.c_str());
        return false;
    }
    
    // Get file size
    struct stat st;
    if (fstat(fd, &st) != 0) {
        ESP_LOGE(TAG, "Failed to stat ELF file");
        close(fd);
        return false;
    }
    
    *out_size = st.st_size;
    
    // Read the entire file into a buffer (PSRAM if available)
    void* elf_data = heap_caps_malloc(st.st_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!elf_data) {
        elf_data = malloc(st.st_size);
        if (!elf_data) {
            ESP_LOGE(TAG, "Failed to allocate memory for ELF");
            close(fd);
            return false;
        }
    }
    
    ssize_t bytes_read = read(fd, elf_data, st.st_size);
    close(fd);
    
    if (bytes_read != st.st_size) {
        ESP_LOGE(TAG, "Failed to read entire ELF file");
        free(elf_data);
        return false;
    }
    
    // Load ELF using esp_elf_loader
    esp_elf_error_t err = esp_elf_load(elf_data, st.st_size, out_handle);
    if (err != ESP_ELF_OK) {
        ESP_LOGE(TAG, "esp_elf_load failed: %d", err);
        free(elf_data);
        return false;
    }
    
    ESP_LOGI(TAG, "ELF loaded successfully, handle: %p", *out_handle);
    free(elf_data);  // The ELF loader makes its own copy
    return true;
}

// ================================================================
// Resolve Symbol
// ================================================================

void* RshellElifLoader::resolve_symbol(void* handle, const char* symbol_name) {
    if (!handle || !symbol_name) return nullptr;
    
    void* symbol = esp_elf_get_symbol_addr(handle, symbol_name);
    if (!symbol) {
        ESP_LOGW(TAG, "Symbol not found: %s", symbol_name);
    }
    return symbol;
}

// ================================================================
// Create App from ELF
// ================================================================

bool RshellElifLoader::create_app_from_elf(void* handle, const std::string& name,
                                           std::shared_ptr<AppBase>& out_app) {
    // Look for the required symbols
    // Option 1: Direct AppFactory function
    auto* factory = (AppFactoryFunc)resolve_symbol(handle, "create_app");
    
    if (factory) {
        // Build config
        ApplicationConfig cfg;
        cfg.name = name.c_str();
        cfg.stack_size_bytes = 8192;
        cfg.priority = 5;
        cfg.tick_rate_hz = 10;
        cfg.capabilities = static_cast<uint32_t>(AppCapability::FULLSCREEN) |
                           static_cast<uint32_t>(AppCapability::NEEDS_WINDOW);
        
        out_app = factory(cfg);
        if (out_app) {
            ESP_LOGI(TAG, "App created via factory: %s", name.c_str());
            return true;
        }
    }
    
    // Option 2: Find a class constructor (advanced - we'll skip for now)
    ESP_LOGE(TAG, "No create_app symbol found in ELF");
    return false;
}

// ================================================================
// Unload App
// ================================================================

bool RshellElifLoader::unload_app(const std::string& name) {
    auto it = loaded_apps.find(name);
    if (it == loaded_apps.end()) {
        ESP_LOGW(TAG, "App not found: %s", name.c_str());
        return false;
    }
    
    LoadedApp& loaded = it->second;
    
    // Close the app
    if (loaded.app) {
        loaded.app->stop_task();
        loaded.app.reset();
    }
    
    // Unload the ELF
    if (loaded.elf_handle) {
        esp_elf_unload(loaded.elf_handle);
    }
    
    ESP_LOGI(TAG, "Unloaded app: %s", name.c_str());
    loaded_apps.erase(it);
    return true;
}

// ================================================================
// Utility Functions
// ================================================================

bool RshellElifLoader::is_app_loaded(const std::string& name) const {
    return loaded_apps.find(name) != loaded_apps.end();
}

std::vector<std::string> RshellElifLoader::get_loaded_apps() const {
    std::vector<std::string> names;
    for (const auto& [name, loaded] : loaded_apps) {
        names.push_back(name);
    }
    return names;
}

bool RshellElifLoader::register_loaded_app(const std::string& name, 
                                           std::shared_ptr<AppBase> app) {
    if (!app) return false;
    
    // Create a manifest for the dynamic app
    AppManifest manifest;
    manifest.name = name;
    manifest.display_name = name;
    manifest.description = "Dynamic ELF app";
    manifest.capabilities = static_cast<uint32_t>(AppCapability::FULLSCREEN) |
                            static_cast<uint32_t>(AppCapability::NEEDS_WINDOW);
    manifest.stack_size_bytes = 8192;
    manifest.priority = 5;
    manifest.tick_rate_hz = 10;
    manifest.create = [app](const ApplicationConfig& cfg) { return app; };
    
    appManager::instance().register_dynamic_app(manifest);
    return true;
}