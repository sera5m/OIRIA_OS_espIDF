#pragma once
// Boot-time WiFi / BT credential load + connect
// Parses conf → fills env_vars Credential / BLEProfile tables → optional STA connect
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RS_AC_MAX_WIFI 8
#define RS_AC_MAX_BT   4
#define RS_AC_SSID_LEN 32
#define RS_AC_PASS_LEN 64
#define RS_AC_NAME_LEN 32

typedef struct {
    char     ssid[RS_AC_SSID_LEN];
    char     pass[RS_AC_PASS_LEN];
    uint8_t  priority;   // 1 = try first
} rs_ac_wifi_t;

typedef struct {
    char     name[RS_AC_NAME_LEN];
    char     pass[RS_AC_PASS_LEN];
    uint8_t  priority;
} rs_ac_bt_t;

typedef struct {
    rs_ac_wifi_t wifi[RS_AC_MAX_WIFI];
    int          wifi_n;
    rs_ac_bt_t   bt[RS_AC_MAX_BT];
    int          bt_n;
    bool         loaded;
} rs_ac_table_t;

bool rs_ac_parse(const char* text, rs_ac_table_t* out);

// SD path default "/sdcard/conf/auto_connect.txt", then embedded conf blob
bool rs_ac_load(rs_ac_table_t* out, const char* sd_path);

// Copy table into env_vars Credential / BLEProfile globals (tokens live there)
void rs_ac_sync_to_env(const rs_ac_table_t* tab);

bool rs_ac_wifi_connect(const rs_ac_table_t* tab, uint32_t timeout_ms_per_ap);

// load → sync_to_env → wifi_connect
bool rs_ac_boot_try(uint32_t timeout_ms_per_ap);

void rs_ac_set_test_wifi(rs_ac_table_t* tab, const char* ssid, const char* pass);

#ifdef __cplusplus
}
#endif
