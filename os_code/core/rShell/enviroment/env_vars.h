#ifndef ENV_VARS_H
#define ENV_VARS_H

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include <time.h>
#include "esp_timer.h"
#include "code_stuff/types.h"
#include "os_code/middle_layer/input/hid_t.h"

#ifdef __cplusplus
enum class HIDTarget : uint8_t;
#else
typedef uint8_t HIDTarget;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STORAGE_RAM,
    STORAGE_PSRAM,
    STORAGE_NVS,
    STORAGE_MICROSD,
    STORAGE_EXT_NONVOL,
    STORAGE_EXT_VOL,
    STORAGE_SEND_EXTDEV
} e_type_storage;

void datastream_init_from_env(void);

//wireless connects======================================
typedef struct __attribute__((packed)) {
    char username[32];
    char password[64];
    uint8_t autoconnect_priority;
    bool isTwoFactor;
    bool isPerma;
    bool StoreSalted;
    bool StoreMicrosd;
} Credential;

typedef struct __attribute__((packed)) BLEProfile {
    uint8_t mac_address[6];
    uint8_t autoconnect_priority;
} BLEProfile;

// Runtime tables filled by rs_autoconnect (boot / conf file)
#define ENV_WIFI_CRED_MAX 8
#define ENV_BLE_PROF_MAX  4
extern Credential g_wifi_credentials[ENV_WIFI_CRED_MAX];
extern int        g_wifi_credentials_count;
extern BLEProfile g_ble_profiles[ENV_BLE_PROF_MAX];
extern int        g_ble_profiles_count;

typedef struct  ESPNowPeer {
    uint8_t peer_mac[6];
    uint8_t wifi_channel;
    uint8_t autoconnect_priority;
} ESPNowPeer;

typedef struct  MeshtasticProfile {
    uint32_t node_id;
    char channel_name[12];
    uint8_t encryption_key[32];
    uint8_t modem_preset;
} MeshtasticProfile;

typedef struct  SubGhzProfile {
    uint32_t frequency;
    uint32_t modulation;
    float    deviation;
    float    data_rate;
    uint32_t rx_bandwidth;
    uint8_t  sync_word[2];
    uint8_t  priority;
} SubGhzProfile;

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

typedef struct __attribute__((packed)) {
    uint8_t days;
    uint8_t hours;
    uint8_t min;
    uint8_t sec;
} Timer_delay_duration;

extern const char* months[];

typedef struct {
    float temperature;
    float userTemperature;
    float cpuTemp;

    uint16_t cpuMhzTarget;
    uint16_t cpuMhzMin;
    uint16_t cpuMhzMax;
    bool enableCpuScaling;
    bool overclockUnlocked;
    uint8_t cpuLoadPercent;

    uint8_t brightness;
    uint16_t fpsTarget;
    uint16_t framethrottle_target;

    bool headless;
    bool UseFrameThrottle;

    uint16_t  screen_dim_w;
    uint16_t screen_dim_h;
    uint16_t  clamped_screen_dim_w;
    uint16_t clamped_screen_dim_h;

    bool hasMicroSD;
    int32_t extStorageSizeKb;
    uint32_t flashSizeKb;
    uint32_t freeSpaceKb;

    uint8_t batteryPercent;
    bool charging;
    float inputVoltage;
    float systemVoltage;

    HIDTarget CurrentHIDTarget;

    bool safeMode;
    bool debugMode;
    bool factoryMode;

    uint32_t bootCount;
    uint32_t lastCrashCode;
    uint32_t firmwareVersion;

    float tempOffset;
    float screenGamma;

    s_displayTime displayTime;
} EnvConfig;

extern EnvConfig v_env;

uint16_t GetFrameRateTarget();
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

inline float Targetfps_to_ms()
{
    const uint16_t fps = (v_env.UseFrameThrottle)
        ? v_env.framethrottle_target
        : v_env.fpsTarget;
    return (fps != 0) ? (1000.0f / (float)fps) : 0.0f;
}

inline uint16_t Targetfps_to_i_ms()
{
    const uint16_t fps = (v_env.UseFrameThrottle)
        ? v_env.framethrottle_target
        : v_env.fpsTarget;
    return (fps != 0) ? (uint16_t)((1000u + (fps / 2u)) / fps) : (uint16_t)0;
}

static inline float fps_to_ms(uint16_t fps)
{
    return (fps != 0) ? (1000.0f / (float)fps) : 0.0f;
}

static inline uint16_t f_ms_to_fps(float ms)
{
    return (ms > 0.0f) ? (uint16_t)(1000.0f / ms) : (uint16_t)0;
}

static inline uint16_t i_ms_to_fps(uint16_t ms)
{
    return (ms != 0) ? (uint16_t)(1000.0f / (float)ms) : (uint16_t)0;
}

#ifdef __cplusplus
}
#endif
#endif