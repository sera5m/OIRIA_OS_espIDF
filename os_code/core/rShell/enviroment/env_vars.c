#include <stdint.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdbool.h>
#include "os_code/core/rShell/enviroment/env_vars.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_timer.h"
#include "code_stuff/types.h"



const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", //i miss when we had pride month but nooo conservitard cult-ure war
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};




EnvConfig v_env = { //vars_env
    // =========================
    // SYSTEM / TEMPERATURE
    // =========================
    .temperature = 0.0f,
    .userTemperature = 0.0f,
    .cpuTemp = 0.0f,

    // =========================
    // CPU CONTROL
    // =========================
    .cpuMhz = 240,
    .cpuMhzLimit = 240,
    .enableCpuScaling = true,
    .overclockUnlocked = false,
    .cpuLoadPercent = 0,

    // =========================
    // DISPLAY
    // =========================
    .brightness = 128,
    .fpsTarget = 45,
    .displayEnabled = true,
    //change this to the actual screen size in the driver, retard
    //but i do not want to load drivers for screens dynamically rn because i have bigger towers to topple
    .screen_dim_w=240, 
    .screen_dim_h=280,
    //these two variables may change at any time, but screen dim basics won't, so cope harder LLLLIBERRUUUULLLL
    .clamped_screen_dim_w=240, 
    .clamped_screen_dim_h=280, 
    // =========================
    // STORAGE
    // =========================
    .hasMicroSD = false,
    .extStorageSizeKb = 0,
    .flashSizeKb = 0,
    .freeSpaceKb = 0,

    // =========================
    // POWER / BATTERY
    // =========================
    .batteryPercent = 100,
    .charging = false,
    .inputVoltage = 5.0f,
    .systemVoltage = 3.3f,

    // =========================
    // INPUT / UI STATE
    // =========================
    
    // =========================
    // SYSTEM FLAGS
   // =========================
    .safeMode = true,
    .debugMode = false,
    .factoryMode = false,

    // =========================
    // VERSIONING / DEBUG
    // =========================
    .bootCount = 0,
    .lastCrashCode = 0,
    .firmwareVersion = 1,

    // =========================
    // CALIBRATION / OFFSETS
    // =========================
    .tempOffset = 0.0f,
    .screenGamma = 2.2f,


    
    .displayTime={0} //fuck you mcgee we set this null init, it's set in main

};


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