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
#include "code_stuff/types.h"

#include "os_code/middle_layer/input/hid_t.h"
//#include "preferences"

// ✅ Forward declare HIDTarget instead of including full header
#ifdef __cplusplus
enum class HIDTarget : uint8_t;
#else
typedef uint8_t HIDTarget;  // C fallback (or don't use in C)
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
} e_type_storage; //type of storage for data pools.

void datastream_init_from_env(void); //hook to streams




//wireless connects======================================
typedef struct __attribute__((packed)) {
    char username[32]; 
    char password[64]; // or token, etc.
    uint8_t autoconnect_priority; //if not 0, will try to autoconnect to this network in order of 255,254,253.... and so down to 1. 0 means do not autoconnect to this network
    bool isTwoFactor; //implement this per thing, it will differ
    bool isPerma; //if true, this credential is saved in non-volatile storage, otherwise it is just a temporary credential for the current session
    bool StoreSalted; //if true, the password is stored in a salted hash form, otherwise it is stored in plaintext (not recommended)
    bool StoreMicrosd; //if true, this credential is stored on the microSD card, otherwise it is stored in NVS
} Credential;


typedef struct __attribute__((packed)) BLEProfile {
    uint8_t mac_address[6];       // Unique hardware device ID
    uint8_t autoconnect_priority; 
} BLEProfile;
//end_wireless coms

typedef struct  ESPNowPeer {
    uint8_t peer_mac[6];          // Target device MAC address
    uint8_t wifi_channel;         // Must be on the same channel (1-14)
    uint8_t autoconnect_priority; // Use to sort list. if greater than zero, will try to autoconnect to this peer in order of 255,254,253.... 
} ESPNowPeer;

typedef struct  MeshtasticProfile {
    uint32_t node_id;            // Every radio has a unique 4-byte ID number
    char channel_name[12];       // e.g., "LongFast" or "PrivateGroup"
    uint8_t encryption_key[32];  // 256-bit AES key so strangers can't read your texts
    uint8_t modem_preset;        // Radio settings (e.g., Short-Fast or Long-Slow range)
} MeshtasticProfile;

typedef struct  SubGhzProfile {
    uint32_t frequency;       // e.g., 315000000 or 433920000 (in Hz)
    uint32_t modulation;      // 0 = ASK/OOK (Garage), 1 = FSK (Sensors)
    float    deviation;       // Radio wave swing frequency (in kHz)
    float    data_rate;       // Speed of transmission (in kBaud)
    uint32_t rx_bandwidth;    // Receiver listening window size (in kHz)
    uint8_t  sync_word[2];    // Hardware filtering bytes (ignores random noise)
    uint8_t  priority;        // Your autoconnect/scanning priority order
} SubGhzProfile;


//msc

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
    uint16_t cpuMhzTarget;
    uint16_t cpuMhzMin;
    uint16_t cpuMhzMax;
    bool enableCpuScaling;    // dynamic frequency scaling
    bool overclockUnlocked;   // allow unsafe freq ranges
    uint8_t cpuLoadPercent;   // OPTIONAL: current utilization snapshot

    // =========================
    // DISPLAY
    // =========================
    uint8_t brightness;       // backlight level
    uint16_t fpsTarget;        // render cap
    uint16_t framethrottle_target;
    
    bool headless; //don't bother with the screen
    bool UseFrameThrottle;

    uint16_t  screen_dim_w;
    uint16_t screen_dim_h; 

    uint16_t  clamped_screen_dim_w; //i swear to god
    uint16_t clamped_screen_dim_h;
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
    HIDTarget CurrentHIDTarget; //technically uint8 type, so we'll just use that, unfortunately

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

//helpful conversions
// Helpful conversions

inline float Targetfps_to_ms()
{
    const uint16_t fps = (v_env.UseFrameThrottle)
        ? v_env.framethrottle_target
        : v_env.fpsTarget;

    return (fps != 0)
        ? (1000.0f / (float)fps)
        : 0.0f;
}

inline uint16_t Targetfps_to_i_ms()
{
    const uint16_t fps = (v_env.UseFrameThrottle)
        ? v_env.framethrottle_target
        : v_env.fpsTarget;

    return (fps != 0)
        ? (uint16_t)((1000u + (fps / 2u)) / fps)
        : (uint16_t)0;
}

// timing.h

static inline float fps_to_ms(uint16_t fps)
{
    return (fps != 0)
        ? (1000.0f / (float)fps)
        : 0.0f;
}

static inline uint16_t f_ms_to_fps(float ms)
{
    return (ms > 0.0f)
        ? (uint16_t)(1000.0f / ms)
        : (uint16_t)0;
}

static inline uint16_t i_ms_to_fps(uint16_t ms)
{
    return (ms != 0)
        ? (uint16_t)(1000.0f / (float)ms)
        : (uint16_t)0;
}








#ifdef __cplusplus
}
#endif
#endif