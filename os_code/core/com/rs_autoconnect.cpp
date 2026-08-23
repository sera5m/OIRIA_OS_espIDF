// rs_autoconnect.cpp — parse conf, sync into env_vars Credential tables, STA connect
#include "rs_autoconnect.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if __has_include("esp_wifi.h")
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#define RS_AC_HAVE_WIFI 1
#else
#define RS_AC_HAVE_WIFI 0
#endif

static const char* TAG = "rs_ac";

// ESP-IDF EMBED_TXTFILES renames path: dots/slashes → underscores.
// Try several symbol spellings (basename-only embed vs full relative path).
extern const uint8_t _binary_autoconnect_conf_start[]       __attribute__((weak));
extern const uint8_t _binary_autoconnect_conf_end[]         __attribute__((weak));
extern const uint8_t _binary_os_code_core_com_autoconnect_conf_start[] __attribute__((weak));
extern const uint8_t _binary_os_code_core_com_autoconnect_conf_end[]   __attribute__((weak));

static const uint8_t* embed_start(void) {
    if (&_binary_autoconnect_conf_start[0] != &_binary_autoconnect_conf_end[0])
        return _binary_autoconnect_conf_start;
    if (&_binary_os_code_core_com_autoconnect_conf_start[0] !=
        &_binary_os_code_core_com_autoconnect_conf_end[0])
        return _binary_os_code_core_com_autoconnect_conf_start;
    return nullptr;
}
static const uint8_t* embed_end(void) {
    if (&_binary_autoconnect_conf_start[0] != &_binary_autoconnect_conf_end[0])
        return _binary_autoconnect_conf_end;
    if (&_binary_os_code_core_com_autoconnect_conf_start[0] !=
        &_binary_os_code_core_com_autoconnect_conf_end[0])
        return _binary_os_code_core_com_autoconnect_conf_end;
    return nullptr;
}

// ---------------------------------------------------------------------------
static void trim(char* s) {
    if (!s) return;
    char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    char* p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static bool extract_quoted_or_token(const char* src, const char* key, char* out, size_t out_cap) {
    const char* p = strstr(src, key);
    if (!p) return false;
    p += strlen(key);
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < out_cap) out[i++] = *p++;
        out[i] = 0;
        return i > 0;
    }
    size_t i = 0;
    while (*p && *p != ',' && *p != '\n' && *p != '\r' && i + 1 < out_cap) {
        if (isspace((unsigned char)*p) && i > 0) break;
        out[i++] = *p++;
    }
    out[i] = 0;
    trim(out);
    return out[0] != 0;
}

static int line_prio(const char* line) {
    int p = 0;
    if (sscanf(line, "%d:", &p) == 1) return p;
    return 99;
}

bool rs_ac_parse(const char* text, rs_ac_table_t* out) {
    if (!text || !out) return false;
    memset(out, 0, sizeof(*out));

    enum { SEC_NONE, SEC_WIFI, SEC_BT } sec = SEC_NONE;
    char line[192];
    const char* p = text;

    while (*p) {
        size_t li = 0;
        while (*p && *p != '\n' && li + 1 < sizeof line) line[li++] = *p++;
        line[li] = 0;
        if (*p == '\n') p++;

        char* s = line;
        while (*s && isspace((unsigned char)*s)) s++;
        if (!*s || *s == '#') continue;

        if (strstr(s, "wifi_autoconnect_end")) { sec = SEC_NONE; continue; }
        if (strstr(s, "bluetooth_autoconnect_end")) { sec = SEC_NONE; continue; }
        if (strstr(s, "wifi_autoconnect")) { sec = SEC_WIFI; continue; }
        if (strstr(s, "bluetooth_autoconnect")) { sec = SEC_BT; continue; }

        if (sec == SEC_WIFI && out->wifi_n < RS_AC_MAX_WIFI) {
            rs_ac_wifi_t* w = &out->wifi[out->wifi_n];
            w->priority = (uint8_t)line_prio(s);
            if (extract_quoted_or_token(s, "ssid:", w->ssid, sizeof w->ssid) &&
                extract_quoted_or_token(s, "pass:", w->pass, sizeof w->pass)) {
                out->wifi_n++;
            }
        } else if (sec == SEC_BT && out->bt_n < RS_AC_MAX_BT) {
            rs_ac_bt_t* b = &out->bt[out->bt_n];
            b->priority = (uint8_t)line_prio(s);
            bool ok = extract_quoted_or_token(s, "name:", b->name, sizeof b->name);
            if (!ok) ok = extract_quoted_or_token(s, "ssid:", b->name, sizeof b->name);
            extract_quoted_or_token(s, "pass:", b->pass, sizeof b->pass);
            if (ok) out->bt_n++;
        }
    }

    for (int i = 0; i < out->wifi_n; ++i) {
        for (int j = i + 1; j < out->wifi_n; ++j) {
            if (out->wifi[j].priority < out->wifi[i].priority) {
                rs_ac_wifi_t tmp = out->wifi[i];
                out->wifi[i] = out->wifi[j];
                out->wifi[j] = tmp;
            }
        }
    }
    out->loaded = (out->wifi_n > 0 || out->bt_n > 0);
    ESP_LOGI(TAG, "parsed wifi=%d bt=%d", out->wifi_n, out->bt_n);
    return out->loaded;
}

bool rs_ac_load(rs_ac_table_t* out, const char* sd_path) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!sd_path) sd_path = "/sdcard/conf/auto_connect.txt";

    // 1) SD — uses VFS path; card must already be mounted (stage_3_sd_mount / d_sdc)
    FILE* f = fopen(sd_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz < 8192) {
            char* buf = (char*)malloc((size_t)sz + 1);
            if (buf) {
                size_t n = fread(buf, 1, (size_t)sz, f);
                buf[n] = 0;
                fclose(f);
                bool ok = rs_ac_parse(buf, out);
                free(buf);
                if (ok) {
                    ESP_LOGI(TAG, "loaded from %s", sd_path);
                    return true;
                }
            } else {
                fclose(f);
            }
        } else {
            fclose(f);
        }
    }

    // 2) Embedded blob (EMBED_TXTFILES outside SRCS in CMakeLists)
    const uint8_t* es = embed_start();
    const uint8_t* ee = embed_end();
    if (es && ee && ee > es) {
        size_t n = (size_t)(ee - es);
        char* buf = (char*)malloc(n + 1);
        if (buf) {
            memcpy(buf, es, n);
            buf[n] = 0;
            bool ok = rs_ac_parse(buf, out);
            free(buf);
            if (ok) {
                ESP_LOGI(TAG, "loaded embedded autoconnect.conf (%u bytes)", (unsigned)n);
                return true;
            }
        }
    }

    ESP_LOGW(TAG, "no autoconnect config (SD or embed)");
    return false;
}

void rs_ac_sync_to_env(const rs_ac_table_t* tab) {
    if (!tab) return;
    g_wifi_credentials_count = 0;
    g_ble_profiles_count = 0;

    for (int i = 0; i < tab->wifi_n && i < ENV_WIFI_CRED_MAX; ++i) {
        Credential* c = &g_wifi_credentials[i];
        memset(c, 0, sizeof(*c));
        strncpy(c->username, tab->wifi[i].ssid, sizeof c->username - 1);
        strncpy(c->password, tab->wifi[i].pass, sizeof c->password - 1);
        c->autoconnect_priority = tab->wifi[i].priority;
        c->isPerma = true;
        c->StoreMicrosd = true; // came from SD/embed list
        g_wifi_credentials_count++;
    }
    for (int i = 0; i < tab->bt_n && i < ENV_BLE_PROF_MAX; ++i) {
        BLEProfile* b = &g_ble_profiles[i];
        memset(b, 0, sizeof(*b));
        // name stored in mac field area is wrong; keep priority only for now
        // full BT name connect is future — priority stamped
        b->autoconnect_priority = tab->bt[i].priority;
        g_ble_profiles_count++;
    }
    ESP_LOGI(TAG, "synced to env_vars: wifi_creds=%d ble=%d",
             g_wifi_credentials_count, g_ble_profiles_count);
}

void rs_ac_set_test_wifi(rs_ac_table_t* tab, const char* ssid, const char* pass) {
    if (!tab || !ssid) return;
    memset(tab, 0, sizeof(*tab));
    strncpy(tab->wifi[0].ssid, ssid, RS_AC_SSID_LEN - 1);
    if (pass) strncpy(tab->wifi[0].pass, pass, RS_AC_PASS_LEN - 1);
    tab->wifi[0].priority = 1;
    tab->wifi_n = 1;
    tab->loaded = true;
}

#if RS_AC_HAVE_WIFI

static volatile bool s_got_ip = false;

static void on_wifi_event(void*, esp_event_base_t base, int32_t id, void*) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_got_ip = false;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_got_ip = true;
    }
}

static bool wifi_stack_ready = false;

static bool ensure_wifi_stack(void) {
    if (wifi_stack_ready) return true;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
        return false;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_stack_ready = true;
    return true;
}

bool rs_ac_wifi_connect(const rs_ac_table_t* tab, uint32_t timeout_ms_per_ap) {
    if (!tab || tab->wifi_n <= 0) return false;
    if (!ensure_wifi_stack()) return false;
    if (timeout_ms_per_ap < 3000) timeout_ms_per_ap = 8000;

    for (int i = 0; i < tab->wifi_n; ++i) {
        const rs_ac_wifi_t* w = &tab->wifi[i];
        ESP_LOGI(TAG, "try STA ssid='%s' prio=%u", w->ssid, (unsigned)w->priority);

        wifi_config_t wc = {};
        strncpy((char*)wc.sta.ssid, w->ssid, sizeof wc.sta.ssid - 1);
        strncpy((char*)wc.sta.password, w->pass, sizeof wc.sta.password - 1);
        wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        s_got_ip = false;
        esp_wifi_disconnect();
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
        esp_wifi_connect();

        uint32_t waited = 0;
        while (waited < timeout_ms_per_ap) {
            if (s_got_ip) {
                ESP_LOGI(TAG, "connected + IP on '%s'", w->ssid);
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            waited += 100;
        }
        ESP_LOGW(TAG, "timeout on '%s'", w->ssid);
    }
    return false;
}

#else

bool rs_ac_wifi_connect(const rs_ac_table_t* tab, uint32_t) {
    (void)tab;
    ESP_LOGW(TAG, "WiFi not in build");
    return false;
}

#endif

bool rs_ac_boot_try(uint32_t timeout_ms_per_ap) {
    rs_ac_table_t tab{};
    if (!rs_ac_load(&tab, "/sdcard/conf/auto_connect.txt")) {
        return false;
    }
    rs_ac_sync_to_env(&tab);
    return rs_ac_wifi_connect(&tab, timeout_ms_per_ap);
}