// discord_rpc.cpp — Discord webhook mini-lib for OIRIA watch
#include "discord_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "os_code/core/com/rs_autoconnect.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"

static const char* TAG = "discord_rpc";

// Default webhook (override with USR_DISCORD_WEBHOOK / discord_rpc_set_webhook)
static const char* kDefaultWebhook =
    "https://discord.com/api/webhooks/1540797193970257920/3XEm1KlFvlcncDSZo54ulYCutORCOwOq7tNHFWDlFz4er7K3vzSvucgweIJkyI5BeCa3";
//test discord webhook, in an empty discord server
static const char* kDefaultMsg = "THEY glow, you shine -terry davis, probably, i think";

// Test AP used when STA is down (same as boot experiments)
static const char* kTestSsid = "S25u"; //note: this is my phone hotspot, so you fuckers can't grab my wifi
static const char* kTestPass = "bad_opsex"; //sudo install sex with my opps. i mean opsex. i mean opsex. i mean-

char USR_DISCORD_WEBHOOK[256] = {0};

static TaskHandle_t s_rpc_task = nullptr;

struct rpc_job {
    char content[DISCORD_RPC_CONTENT_MAX];
};

void discord_rpc_set_webhook(const char* url) {
    if (!url) {
        USR_DISCORD_WEBHOOK[0] = 0;
        return;
    }
    strncpy(USR_DISCORD_WEBHOOK, url, sizeof USR_DISCORD_WEBHOOK - 1);
    USR_DISCORD_WEBHOOK[sizeof USR_DISCORD_WEBHOOK - 1] = 0;
}

static const char* active_webhook(void) {
    if (USR_DISCORD_WEBHOOK[0]) return USR_DISCORD_WEBHOOK;
    return kDefaultWebhook;
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
static bool ensure_wifi(void) {
    // Prefer already-synced env credentials if present
    if (g_wifi_credentials_count > 0 && g_wifi_credentials[0].username[0]) {
        rs_ac_table_t tab{};
        for (int i = 0; i < g_wifi_credentials_count && i < RS_AC_MAX_WIFI; ++i) {
            strncpy(tab.wifi[i].ssid, g_wifi_credentials[i].username, RS_AC_SSID_LEN - 1);
            strncpy(tab.wifi[i].pass, g_wifi_credentials[i].password, RS_AC_PASS_LEN - 1);
            tab.wifi[i].priority = g_wifi_credentials[i].autoconnect_priority
                                       ? g_wifi_credentials[i].autoconnect_priority
                                       : (uint8_t)(i + 1);
            tab.wifi_n++;
        }
        tab.loaded = tab.wifi_n > 0;
        ESP_LOGI(TAG, "wifi: using %d env credential(s), try '%s'",
                 tab.wifi_n, tab.wifi[0].ssid);
        int64_t t0 = esp_timer_get_time();
        bool ok = rs_ac_wifi_connect(&tab, 15000);
        ESP_LOGI(TAG, "wifi: %s in %lld ms", ok ? "CONNECTED" : "FAILED",
                 (long long)((esp_timer_get_time() - t0) / 1000));
        if (ok) return true;
    }

    ESP_LOGI(TAG, "wifi: fallback test AP ssid='%s'", kTestSsid);
    rs_ac_table_t tab{};
    rs_ac_set_test_wifi(&tab, kTestSsid, kTestPass);
    rs_ac_sync_to_env(&tab);
    int64_t t0 = esp_timer_get_time();
    bool ok = rs_ac_wifi_connect(&tab, 15000);
    ESP_LOGI(TAG, "wifi: %s in %lld ms", ok ? "CONNECTED" : "FAILED",
             (long long)((esp_timer_get_time() - t0) / 1000));
    return ok;
}

// ---------------------------------------------------------------------------
// HTTP POST
// ---------------------------------------------------------------------------
static bool http_post_webhook(const char* content) {
    if (!content || !content[0]) content = kDefaultMsg;

    char body[320];
    char escaped[240];
    size_t j = 0;
    for (size_t i = 0; content[i] && j + 2 < sizeof escaped; ++i) {
        char c = content[i];
        if (c == '"' || c == '\\') {
            escaped[j++] = '\\';
            escaped[j++] = c;
        } else if (c == '\n') {
            escaped[j++] = '\\';
            escaped[j++] = 'n';
        } else if ((unsigned char)c >= 32) {
            escaped[j++] = c;
        }
    }
    escaped[j] = 0;
    snprintf(body, sizeof body, "{\"content\":\"%s\"}", escaped);

    const char* url = active_webhook();
    ESP_LOGI(TAG, "discord: POST %u bytes → webhook", (unsigned)strlen(body));

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 15000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "discord: http client init failed");
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "discord: POST err=%s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "discord: HTTP %d %s", status,
             (status >= 200 && status < 300) ? "(ok)" : "(fail)");
    return (status >= 200 && status < 300);
}

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------
static void rpc_task(void* arg) {
    rpc_job* job = (rpc_job*)arg;
    ESP_LOGI(TAG, "discord_rpc: task start");
    (void)ensure_wifi();
    vTaskDelay(pdMS_TO_TICKS(800));
    bool ok = http_post_webhook(job ? job->content : kDefaultMsg);
    ESP_LOGI(TAG, "discord_rpc: done ok=%d", (int)ok);
    free(job);
    s_rpc_task = nullptr;
    vTaskDelete(nullptr);
}

static void spawn_rpc(const char* content) {
    rpc_job* job = (rpc_job*)calloc(1, sizeof(*job));
    if (!job) return;
    strncpy(job->content, content && content[0] ? content : kDefaultMsg,
            sizeof job->content - 1);

    if (s_rpc_task) {
        ESP_LOGW(TAG, "discord_rpc: previous task still running — spawning another");
    }

    BaseType_t r = xTaskCreate(rpc_task, "discord_rpc", 16384, job, 5, &s_rpc_task);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "discord_rpc: task create failed");
        s_rpc_task = nullptr;
        free(job);
    } else {
        ESP_LOGI(TAG, "discord_rpc: worker spawned (16KB)");
    }
}

void send_discord_rpc(void) {
    spawn_rpc(kDefaultMsg);
}

void send_discord_rpc_msg(const char* content) {
    spawn_rpc(content);
}

void send_discord_rpc_buf(const char* data, size_t len) {
    if (!data || !len) {
        spawn_rpc(kDefaultMsg);
        return;
    }
    char tmp[DISCORD_RPC_CONTENT_MAX];
    size_t n = len < sizeof tmp - 1 ? len : sizeof tmp - 1;
    memcpy(tmp, data, n);
    tmp[n] = 0;
    spawn_rpc(tmp);
}

bool despawn_discord_rpc(void) {
    TaskHandle_t t = s_rpc_task;
    if (!t) return false;
    s_rpc_task = nullptr;
    vTaskDelete(t);
    ESP_LOGW(TAG, "discord_rpc: task force-deleted");
    return true;
}

// ---------------------------------------------------------------------------
// Minimal CBOR → text (RFC 8949 major types 0–5,7 common simple values)
// ---------------------------------------------------------------------------
struct cbor_cursor {
    const uint8_t* p;
    const uint8_t* end;
};

static bool cbor_take(cbor_cursor* c, uint8_t* out) {
    if (c->p >= c->end) return false;
    *out = *c->p++;
    return true;
}

static bool cbor_uval(cbor_cursor* c, uint8_t ai, uint64_t* v) {
    if (ai < 24) {
        *v = ai;
        return true;
    }
    size_t n = (ai == 24) ? 1 : (ai == 25) ? 2 : (ai == 26) ? 4 : (ai == 27) ? 8 : 0;
    if (!n || (size_t)(c->end - c->p) < n) return false;
    uint64_t x = 0;
    for (size_t i = 0; i < n; ++i) x = (x << 8) | *c->p++;
    *v = x;
    return true;
}

static bool cbor_append(char* out, size_t cap, size_t* o, const char* s) {
    size_t n = strlen(s);
    if (*o + n >= cap) return false;
    memcpy(out + *o, s, n);
    *o += n;
    out[*o] = 0;
    return true;
}

static bool cbor_append_fmt(char* out, size_t cap, size_t* o, const char* fmt, ...) {
    char tmp[64];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return false;
    return cbor_append(out, cap, o, tmp);
}

// forward
static bool cbor_item(cbor_cursor* c, char* out, size_t cap, size_t* o, int depth);

static bool cbor_item(cbor_cursor* c, char* out, size_t cap, size_t* o, int depth) {
    if (depth > 8 || c->p >= c->end) return false;
    uint8_t ib;
    if (!cbor_take(c, &ib)) return false;
    uint8_t mt = ib >> 5;
    uint8_t ai = ib & 0x1f;
    uint64_t v = 0;

    switch (mt) {
    case 0: // unsigned
        if (!cbor_uval(c, ai, &v)) return false;
        return cbor_append_fmt(out, cap, o, "%llu", (unsigned long long)v);
    case 1: // negative
        if (!cbor_uval(c, ai, &v)) return false;
        return cbor_append_fmt(out, cap, o, "-%llu", (unsigned long long)(v + 1));
    case 2: // bytes → hex
        if (!cbor_uval(c, ai, &v) || (size_t)(c->end - c->p) < v) return false;
        if (!cbor_append(out, cap, o, "h'")) return false;
        for (uint64_t i = 0; i < v && i < 32; ++i) {
            if (!cbor_append_fmt(out, cap, o, "%02X", c->p[i])) return false;
        }
        c->p += v;
        return cbor_append(out, cap, o, (v > 32) ? "…'" : "'");
    case 3: { // text
        if (!cbor_uval(c, ai, &v) || (size_t)(c->end - c->p) < v) return false;
        if (!cbor_append(out, cap, o, "\"")) return false;
        for (uint64_t i = 0; i < v; ++i) {
            char ch = (char)c->p[i];
            if (ch == '"' || ch == '\\') {
                if (!cbor_append(out, cap, o, "\\")) return false;
            }
            char one[2] = {ch, 0};
            if ((unsigned char)ch >= 32) {
                if (!cbor_append(out, cap, o, one)) return false;
            }
        }
        c->p += v;
        return cbor_append(out, cap, o, "\"");
    }
    case 4: { // array
        if (!cbor_uval(c, ai, &v)) return false;
        if (!cbor_append(out, cap, o, "[")) return false;
        for (uint64_t i = 0; i < v; ++i) {
            if (i && !cbor_append(out, cap, o, ", ")) return false;
            if (!cbor_item(c, out, cap, o, depth + 1)) return false;
        }
        return cbor_append(out, cap, o, "]");
    }
    case 5: { // map
        if (!cbor_uval(c, ai, &v)) return false;
        if (!cbor_append(out, cap, o, "{")) return false;
        for (uint64_t i = 0; i < v; ++i) {
            if (i && !cbor_append(out, cap, o, ", ")) return false;
            if (!cbor_item(c, out, cap, o, depth + 1)) return false;
            if (!cbor_append(out, cap, o, ": ")) return false;
            if (!cbor_item(c, out, cap, o, depth + 1)) return false;
        }
        return cbor_append(out, cap, o, "}");
    }
    case 7:
        if (ai == 20) return cbor_append(out, cap, o, "false");
        if (ai == 21) return cbor_append(out, cap, o, "true");
        if (ai == 22) return cbor_append(out, cap, o, "null");
        if (ai == 23) return cbor_append(out, cap, o, "undefined");
        if (ai == 26 && (size_t)(c->end - c->p) >= 4) { // float32
            union { uint32_t u; float f; } u;
            u.u = ((uint32_t)c->p[0] << 24) | ((uint32_t)c->p[1] << 16) |
                  ((uint32_t)c->p[2] << 8) | (uint32_t)c->p[3];
            c->p += 4;
            return cbor_append_fmt(out, cap, o, "%.4g", (double)u.f);
        }
        return cbor_append_fmt(out, cap, o, "simple(%u)", (unsigned)ai);
    default:
        return false;
    }
}

size_t cbor_to_discord_msg(const uint8_t* cbor, size_t cbor_len, char* out, size_t out_cap) {
    if (!out || out_cap < 2) return 0;
    out[0] = 0;
    if (!cbor || !cbor_len) {
        strncpy(out, "(empty cbor)", out_cap - 1);
        return strlen(out);
    }
    size_t o = 0;
    cbor_cursor cur{cbor, cbor + cbor_len};
    if (!cbor_append(out, out_cap, &o, "sensor: ")) {
        return o;
    }
    if (!cbor_item(&cur, out, out_cap, &o, 0)) {
        // hex fallback
        o = 0;
        cbor_append(out, out_cap, &o, "cbor:");
        size_t maxb = cbor_len < 48 ? cbor_len : 48;
        for (size_t i = 0; i < maxb; ++i)
            cbor_append_fmt(out, out_cap, &o, "%02X", cbor[i]);
        if (cbor_len > maxb) cbor_append(out, out_cap, &o, "…");
    }
    return o;
}

bool send_discord_rpc_cbor(const uint8_t* cbor, size_t cbor_len) {
    char msg[DISCORD_RPC_CONTENT_MAX];
    size_t n = cbor_to_discord_msg(cbor, cbor_len, msg, sizeof msg);
    if (!n) return false;
    spawn_rpc(msg);
    return true;
}
