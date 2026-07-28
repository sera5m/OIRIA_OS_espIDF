#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "esp_elf.h"
#include "esp_elf_adapter.h"
#include "esp_elf_symbol.h"

// Dynamic app loading via ELF / binary on SD card
class RshellElifLoader {
public:
    static RshellElifLoader& instance();
    
    // Load an ELF app from SD card
    std::shared_ptr<AppBase> load_from_sd(const std::string& path);
    
    // Unload an app and free its resources
    bool unload_app(const std::string& name);
    
    // Check if an app is loaded
    bool is_app_loaded(const std::string& name) const;
    
    // Get all loaded dynamic apps
    std::vector<std::string> get_loaded_apps() const;
    
    // Register a loaded app with the app manager
    bool register_loaded_app(const std::string& name, std::shared_ptr<AppBase> app);

private:
    RshellElifLoader() = default;
    ~RshellElifLoader() = default;
    
    // Loaded app info
    struct LoadedApp {
        std::string name;
        std::shared_ptr<AppBase> app;
        void* elf_handle = nullptr;
        size_t memory_used = 0;
        std::string elf_path;
    };
    
    std::unordered_map<std::string, LoadedApp> loaded_apps;
    
    // Internal helpers
    bool load_elf_file(const std::string& path, void** out_handle, size_t* out_size);
    void* resolve_symbol(void* handle, const char* symbol_name);
    bool create_app_from_elf(void* handle, const std::string& name, 
                             std::shared_ptr<AppBase>& out_app);
};

// The app entry point that every ELF app must export
using AppFactoryFunc = std::shared_ptr<AppBase>(*)(const ApplicationConfig&);