#pragma once

#include <string>
#include <memory>
#include "os_code/core/rShell/rshell_appFramework.hpp"

// Dynamic app loading via ELF / binary on SD card
class RshellElifLoader {
public:
    static std::shared_ptr<AppBase> load_from_sd(const std::string& path);
    static bool unload_app(const std::string& name);

    // Future: symbol resolution, relocation, etc.
};
