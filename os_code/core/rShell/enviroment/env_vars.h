#ifndef ENV_VARS_H
#define ENV_VARS_H

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdbool.h>
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include <time.h>
#include "esp_timer.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct __attribute__((packed)) {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    bool use24h_time;
    uint8_t hh;
    uint8_t mm;
    uint8_t ss;
    uint16_t ms; 
} s_displayTime;

extern const char* months[];


typedef struct {
    // =========================
    // SYSTEM / TEMPERATURE
    // =========================
    float temperature;        // general ambient/system temp
    float userTemperature;    // user-defined target or calibration offset
    float cpuTemp;            // CPU die temp

    // =========================
    // CPU CONTROL
    // =========================
    uint16_t cpuMhz;
    uint16_t cpuMhzLimit;
    bool enableCpuScaling;    // dynamic frequency scaling
    bool overclockUnlocked;   // allow unsafe freq ranges
    uint8_t cpuLoadPercent;   // OPTIONAL: current utilization snapshot

    // =========================
    // DISPLAY
    // =========================
    uint8_t brightness;       // backlight level
    uint8_t fpsTarget;        // render cap
    bool displayEnabled;
    uint16_t  screen_dim_w;
    uint16_t screen_dim_h; 
    // =========================
    // STORAGE
    // =========================
    bool hasMicroSD;
    int32_t extStorageSizeKb;  // external storage size
    uint32_t flashSizeKb;      // internal flash size (often forgotten)
    uint32_t freeSpaceKb;      // runtime snapshot (optional but useful)

    // =========================
    // POWER / BATTERY
    // =========================
    uint8_t batteryPercent;
    bool charging;
    float inputVoltage;
    float systemVoltage;

    // =========================
    // INPUT / UI STATE
    // =========================
    

    // =========================
    // SYSTEM FLAGS
    // =========================
    bool safeMode;
    bool debugMode;
    bool factoryMode;

    // =========================
    // VERSIONING / DEBUG
    // =========================
    uint32_t bootCount;
    uint32_t lastCrashCode;
    uint32_t firmwareVersion;

    // =========================
    // CALIBRATION / OFFSETS
    // =========================
    float tempOffset;
    float screenGamma;
    //====================
    //more system data globals
    //====================
    s_displayTime displayTime; //human readable display time
} EnvConfig;

extern EnvConfig v_env; //current configuration
//note to self add a defaults for hot reload


void update_display_time(s_displayTime *t);

typedef enum {
    DATA_NONE = 0,
    DATA_INT,
    DATA_FLOAT,
    DATA_STRING,
    DATA_RAW
} DataType;

typedef struct {
    char name[16];

    DataType type;
    uint16_t size;

    union {
        int32_t i;
        float f;
        char str[32];
        uint8_t raw[64];
    } data;

} ArbitraryUserData;


#ifdef __cplusplus
}
#endif
#endif