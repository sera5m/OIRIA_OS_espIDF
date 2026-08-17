#include "boot_role.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char* TAG = "boot_role";

// Optional: force at compile time
// #define CONFIG_RS_BOOT_ROLE  BOOT_ROLE_PUPPET

const char* boot_role_name(boot_role_t r) {
    switch (r) {
        case BOOT_ROLE_TYRANT: return "TYRANT";
        case BOOT_ROLE_PUPPET: return "PUPPET";
        default:               return "SOLO";
    }
}

boot_role_t boot_role_resolve(void) {
#ifdef CONFIG_RS_BOOT_ROLE
    boot_role_t forced = (boot_role_t)CONFIG_RS_BOOT_ROLE;
    ESP_LOGI(TAG, "role from Kconfig: %s", boot_role_name(forced));
    return forced;
#endif

    // NVS override (set once from a setup app or idf.py)
    nvs_handle_t h;
    if (nvs_open("rshell", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 0xFF;
        if (nvs_get_u8(h, "boot_role", &v) == ESP_OK && v <= BOOT_ROLE_PUPPET) {
            nvs_close(h);
            ESP_LOGI(TAG, "role from NVS: %s", boot_role_name((boot_role_t)v));
            return (boot_role_t)v;
        }
        nvs_close(h);
    }

    // env flag (can be set by earlier boot stage / factory)
    if (v_env.headless) {
        ESP_LOGI(TAG, "role from v_env.headless → PUPPET");
        return BOOT_ROLE_PUPPET;
    }

    ESP_LOGI(TAG, "role default → SOLO");
    return BOOT_ROLE_SOLO;
}
