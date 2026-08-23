

#include <stdbool.h>
#include <stdint.h>
#include "os_code/core/rShell/enviroment/env_vars.h"
#include <time.h>
#include "esp_timer.h"
#include "code_stuff/types.h"
#include "os_code/middle_layer/input/hid_t.h"

const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May",
     "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Autoconnect token tables (filled by rs_ac_sync_to_env)
Credential g_wifi_credentials[ENV_WIFI_CRED_MAX];
int        g_wifi_credentials_count = 0;
BLEProfile g_ble_profiles[ENV_BLE_PROF_MAX];
int        g_ble_profiles_count = 0;

EnvConfig v_env = {
    .temperature = 0.0f,
    .userTemperature = 0.0f,
    .cpuTemp = 0.0f,

    .cpuMhzTarget = 240,
    .cpuMhzMin = 160,
    .cpuMhzMax=240,
    .enableCpuScaling = true,
    .overclockUnlocked = false,
    .cpuLoadPercent = 0,

    .brightness = 128,
    .fpsTarget = 45,
    .framethrottle_target=5,
    .headless=false,
    .UseFrameThrottle=false,
    .screen_dim_w=280,
    .screen_dim_h=240,
    .clamped_screen_dim_w=280,
    .clamped_screen_dim_h=240,

    .hasMicroSD = false,
    .extStorageSizeKb = 0,
    .flashSizeKb = 0,
    .freeSpaceKb = 0,

    .batteryPercent = 100,
    .charging = false,
    .inputVoltage = 5.0f,
    .systemVoltage = 3.3f,

    .CurrentHIDTarget = 5,

    .safeMode = true,
    .debugMode = false,
    .factoryMode = false,

    .bootCount = 0,
    .lastCrashCode = 0,
    .firmwareVersion = 1,

    .tempOffset = 0.0f,
    .screenGamma = 2.2f,

    .displayTime={0}
};

uint16_t GetFrameRateTarget() {
return (v_env.UseFrameThrottle ? v_env.framethrottle_target : v_env.fpsTarget);
}

void update_display_time(s_displayTime *t) {
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    t->year = timeinfo.tm_year + 1900;
    t->month = timeinfo.tm_mon + 1;
    t->day = timeinfo.tm_mday;

    t->hh = timeinfo.tm_hour;
    t->mm = timeinfo.tm_min;
    t->ss = timeinfo.tm_sec;

    t->use24h_time = true;

    int64_t us = esp_timer_get_time();
    t->ms = (us / 1000) % 1000;
}