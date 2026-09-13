/* Vulcan web console: drop .vul, corz-style wave, Bojan-style scope. */
#include "rs_vulcan_httpd.h"
#include "rs_autoconnect.hpp"
#include "boot_role.hpp"
#include "code_stuff/signal_gen/siggen_play.h"
#include "os_code/core/window_env/rs_dom_link.hpp"

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>

static const char* TAG = "vul_http";
static httpd_handle_t s_httpd;
static bool s_up;

extern int rs_coll_eval_src(const char* src, size_t len);

static const char INDEX_HTML[] =
"<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>OIRIA Vulcan</title>"
"<style>"
"body{margin:0;background:#0e1116;color:#e6edf3;font:14px/1.4 system-ui,sans-serif}"
"header{padding:10px 16px;background:#161b22;border-bottom:1px solid #30363d;display:flex;gap:12px;align-items:center}"
"h1{font-size:16px;margin:0;font-weight:600} .role{opacity:.7}"
"nav button{background:#21262d;color:#e6edf3;border:1px solid #30363d;border-radius:6px;padding:6px 10px;cursor:pointer}"
"nav button.on{background:#1f6feb;border-color:#1f6feb}"
"main{display:grid;grid-template-columns:1fr 1fr;gap:12px;padding:12px}"
"@media(max-width:800px){main{grid-template-columns:1fr}}"
"section{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:12px}"
"textarea,input,select{width:100%;box-sizing:border-box;background:#0d1117;color:#e6edf3;border:1px solid #30363d;border-radius:6px;padding:8px}"
"textarea{min-height:180px;font-family:ui-monospace,monospace}"
".drop{border:2px dashed #30363d;border-radius:8px;padding:18px;text-align:center;margin-bottom:8px;color:#8b949e}"
".drop.over{border-color:#1f6feb;color:#e6edf3}"
"button.go{background:#238636;border:0;color:#fff;padding:8px 14px;border-radius:6px;margin-top:8px;cursor:pointer}"
"pre{background:#0d1117;padding:8px;border-radius:6px;max-height:140px;overflow:auto;white-space:pre-wrap}"
"canvas{width:100%;height:140px;background:#0d1117;border-radius:6px}"
"label{display:block;margin:8px 0 4px;color:#8b949e;font-size:12px}"
".row{display:flex;gap:8px;flex-wrap:wrap;align-items:end}"
".row>div{flex:1;min-width:90px}"
"</style></head><body>"
"<header><h1>OIRIA Vulcan</h1><span class=role id=role></span>"
"<nav><button class=on id=t1>Script</button><button id=t2>Wave</button><button id=t3>Scope</button></nav>"
"</header><main>"
"<section id=p1>"
"<div class=drop id=drop>drop a .vul here, or paste below</div>"
"<textarea id=src placeholder='print(1);\\nnative(\"wave\", 1, 1000, 50, 4, 80);'></textarea>"
"<div class=row>"
"<div><label>save to</label><select id=dest><option value=sd>microSD /sdcard/inbox</option><option value=ram>PSRAM / RAM (run only)</option></select></div>"
"<div><label>filename</label><input id=fn value=paste.vul></div>"
"</div>"
"<button class=go id=run>Save + Run</button>"
"<button class=go style=background:#1f6feb id=eval>Run without save</button>"
"<pre id=out></pre>"
"</section>"
"<section id=p2>"
"<p>PWM function generator (S3 has no DAC — add an RC on the pin). Corz shortcuts: s r t w 2k p25 a2 stop</p>"
"<div class=row>"
"<div><label>wave</label><select id=wave><option value=0>sine</option><option value=1>square</option><option value=2>triangle</option><option value=3>saw</option></select></div>"
"<div><label>Hz</label><input id=hz type=number value=1000></div>"
"<div><label>duty %</label><input id=duty type=number value=50></div>"
"<div><label>pin</label><input id=pin type=number value=4></div>"
"<div><label>amp %</label><input id=amp type=number value=80></div>"
"</div>"
"<button class=go id=play>Start</button>"
"<button class=go style=background:#da3633 id=stop>Stop</button>"
"<label>corz command</label><input id=corz placeholder='s  or  2k  or  sweep 100 2k 2000'>"
"<button class=go id=corzgo>Send</button>"
"<pre id=wstat></pre>"
"</section>"
"<section id=p3 style=grid-column:1/-1>"
"<div class=row>"
"<div><label>scope pin (ADC1)</label><input id=spin type=number value=1></div>"
"<div><label>samples</label><input id=sn type=number value=200></div>"
"</div>"
"<button class=go id=cap>Capture</button>"
"<canvas id=cv width=800 height=140></canvas>"
"<pre id=sstat></pre>"
"</section>"
"</main>"
"<script>"
"const $=s=>document.querySelector(s);"
"fetch('/status').then(r=>r.json()).then(j=>{$('#role').textContent=j.role+' · '+j.ip;}).catch(()=>{});"
"const log=(el,t)=>$(el).textContent=t;"
"$('#drop').ondragover=e=>{e.preventDefault();e.target.classList.add('over')};"
"$('#drop').ondragleave=e=>e.target.classList.remove('over');"
"$('#drop').ondrop=async e=>{e.preventDefault();e.target.classList.remove('over');"
" const f=e.dataTransfer.files[0]; if(!f)return; $('#fn').value=f.name; $('#src').value=await f.text();};"
"$('#run').onclick=async()=>{"
" const body=$('#src').value; const fn=$('#fn').value||'paste.vul'; const dest=$('#dest').value;"
" const r=await fetch('/vul?dest='+dest+'&name='+encodeURIComponent(fn)+'&run=1',{method:'POST',headers:{'content-type':'text/plain'},body});"
" log('#out', await r.text());};"
"$('#eval').onclick=async()=>{"
" const r=await fetch('/run',{method:'POST',headers:{'content-type':'text/plain'},body:$('#src').value});"
" log('#out', await r.text());};"
"$('#play').onclick=async()=>{"
" const q=`wave=${$('#wave').value}&hz=${$('#hz').value}&duty=${$('#duty').value}&pin=${$('#pin').value}&amp=${$('#amp').value}`;"
" const r=await fetch('/wave?'+q,{method:'POST'}); log('#wstat', await r.text());};"
"$('#stop').onclick=async()=>{log('#wstat', await (await fetch('/wave/stop',{method:'POST'})).text());};"
"$('#corzgo').onclick=async()=>{"
" const r=await fetch('/cmd',{method:'POST',headers:{'content-type':'text/plain'},body:$('#corz').value});"
" log('#wstat', await r.text());};"
"$('#cap').onclick=async()=>{"
" const r=await fetch('/scope.json?pin='+$('#spin').value+'&n='+$('#sn').value);"
" const j=await r.json(); log('#sstat', j.n+' samples  pin '+j.pin);"
" const c=$('#cv'),x=c.getContext('2d'),w=c.width,h=c.height;"
" x.fillStyle='#0d1117';x.fillRect(0,0,w,h); const a=j.mv||[]; if(!a.length)return;"
" let mn=Math.min(...a),mx=Math.max(...a); if(mn===mx){mx=mn+1}"
" x.strokeStyle='#3fb950';x.beginPath();"
" a.forEach((v,i)=>{const px=i/(a.length-1)*w, py=h- (v-mn)/(mx-mn)*h; i?x.lineTo(px,py):x.moveTo(px,py)});"
" x.stroke();};"
"</script></body></html>";

static bool wifi_sta_or_ap(void) {
    /* Try known APs first. */
    if (rs_ac_boot_try(8000)) return true;

    ESP_LOGW(TAG, "STA failed — starting AP OIRIA-vulcan");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    wifi_config_t ap = {0};
    memcpy(ap.ap.ssid, "OIRIA-vulcan", 12);
    memcpy(ap.ap.password, "vulcanvulcan", 12);
    ap.ap.ssid_len = 12;
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    esp_wifi_start();
    ESP_LOGI(TAG, "AP ssid=OIRIA-vulcan pass=vulcanvulcan");
    return true;
}

static char s_ip[24];

static void fill_ip(void) {
    s_ip[0] = 0;
    esp_netif_t* n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!n) n = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!n) { strncpy(s_ip, "0.0.0.0", sizeof s_ip); return; }
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(n, &ip) == ESP_OK)
        snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&ip.ip));
}

static esp_err_t send_text(httpd_req_t* req, const char* s, const char* type) {
    httpd_resp_set_type(req, type ? type : "text/plain");
    return httpd_resp_send(req, s, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_index(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_status(httpd_req_t* req) {
    fill_ip();
    char buf[192];
    const siggen_cfg_t* c = siggen_play_cfg();
    snprintf(buf, sizeof buf,
             "{\"role\":\"%s\",\"ip\":\"%s\",\"wave\":%d,\"hz\":%lu,\"gpio\":%d,\"running\":%s}",
             boot_role_name(boot_role_resolve()), s_ip,
             c ? (int)c->wave : 0,
             c ? (unsigned long)c->freq_hz : 0,
             siggen_play_gpio(),
             (c && c->running) ? "true" : "false");
    return send_text(req, buf, "application/json");
}

static int recv_body(httpd_req_t* req, char** out, int max_n) {
    int len = req->content_len;
    if (len <= 0 || len > max_n) return -1;
    char* buf = NULL;
    if (len > 4096 && esp_psram_is_initialized())
        buf = (char*)heap_caps_malloc((size_t)len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (char*)malloc((size_t)len + 1);
    if (!buf) return -1;
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r <= 0) { free(buf); return -1; }
        got += r;
    }
    buf[len] = 0;
    *out = buf;
    return len;
}

static esp_err_t h_run(httpd_req_t* req) {
    char* body = NULL;
    int n = recv_body(req, &body, 64 * 1024);
    if (n < 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body"); return ESP_FAIL; }
    int rc = rs_coll_eval_src(body, (size_t)n);
    free(body);
    char msg[64];
    snprintf(msg, sizeof msg, "eval %s (%d)\n", rc == 0 ? "ok" : "fail", rc);
    return send_text(req, msg, "text/plain");
}

static esp_err_t h_vul(httpd_req_t* req) {
    char q[96] = {0};
    httpd_req_get_url_query_str(req, q, sizeof q);
    char dest[8] = "sd";
    char name[40] = "paste.vul";
    char run[4] = "0";
    httpd_query_key_value(q, "dest", dest, sizeof dest);
    httpd_query_key_value(q, "name", name, sizeof name);
    httpd_query_key_value(q, "run", run, sizeof run);
    /* sanitize name */
    for (char* p = name; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p=='.' || *p=='_' || *p=='-')) *p = '_';

    char* body = NULL;
    int n = recv_body(req, &body, 96 * 1024);
    if (n < 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body"); return ESP_FAIL; }

    char reply[192];
    if (!strcmp(dest, "ram")) {
        snprintf(reply, sizeof reply, "ram %d bytes (not persisted)\n", n);
    } else {
        mkdir("/sdcard/inbox", 0775);
        char path[128];
        snprintf(path, sizeof path, "/sdcard/inbox/%s", name);
        FILE* f = fopen(path, "wb");
        if (!f) {
            free(body);
            return send_text(req, "sd write failed (card?)\n", "text/plain");
        }
        fwrite(body, 1, (size_t)n, f);
        fclose(f);
        snprintf(reply, sizeof reply, "saved %s (%d bytes)\n", path, n);
    }
    if (run[0] == '1') {
        int rc = rs_coll_eval_src(body, (size_t)n);
        char extra[48];
        snprintf(extra, sizeof extra, "eval %s (%d)\n", rc == 0 ? "ok" : "fail", rc);
        strncat(reply, extra, sizeof reply - strlen(reply) - 1);
    }
    free(body);
    return send_text(req, reply, "text/plain");
}

static int q_int(httpd_req_t* req, const char* key, int def) {
    char q[160] = {0}, v[24] = {0};
    httpd_req_get_url_query_str(req, q, sizeof q);
    if (httpd_query_key_value(q, key, v, sizeof v) == ESP_OK) return atoi(v);
    return def;
}

static esp_err_t h_wave(httpd_req_t* req) {
    int wave = q_int(req, "wave", 1);
    int hz   = q_int(req, "hz", 1000);
    int duty = q_int(req, "duty", 50);
    int pin  = q_int(req, "pin", SIGGEN_DEFAULT_OUT_GPIO);
    int amp  = q_int(req, "amp", 80);
    esp_err_t e = siggen_play(wave, (uint32_t)hz, (uint8_t)duty, pin, (uint8_t)amp);
    char buf[96];
    snprintf(buf, sizeof buf, "wave=%d hz=%d pin=%d %s\n", wave, hz, pin,
             e == ESP_OK ? "ok" : esp_err_to_name(e));
    return send_text(req, buf, "text/plain");
}

static esp_err_t h_wave_stop(httpd_req_t* req) {
    siggen_play_stop();
    return send_text(req, "stop\n", "text/plain");
}

static esp_err_t h_cmd(httpd_req_t* req) {
    char* body = NULL;
    int n = recv_body(req, &body, 256);
    if (n < 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "cmd"); return ESP_FAIL; }
    char out[96];
    siggen_corz_cmd(body, out, sizeof out);
    free(body);
    return send_text(req, out, "text/plain");
}

static esp_err_t h_scope(httpd_req_t* req) {
    int pin = q_int(req, "pin", SIGGEN_DEFAULT_SCOPE_GPIO);
    int n   = q_int(req, "n", 200);
    if (n > 400) n = 400;
    int16_t* mv = (int16_t*)malloc((size_t)n * sizeof(int16_t));
    if (!mv) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }
    int got = siggen_scope_cap(pin, n, mv, NULL);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"pin\":");
    char num[16];
    snprintf(num, sizeof num, "%d", pin);
    httpd_resp_sendstr_chunk(req, num);
    httpd_resp_sendstr_chunk(req, ",\"n\":");
    snprintf(num, sizeof num, "%d", got);
    httpd_resp_sendstr_chunk(req, num);
    httpd_resp_sendstr_chunk(req, ",\"mv\":[");
    for (int i = 0; i < got; i++) {
        snprintf(num, sizeof num, "%s%d", i ? "," : "", (int)mv[i]);
        httpd_resp_sendstr_chunk(req, num);
    }
    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);
    free(mv);
    return ESP_OK;
}

static esp_err_t h_ls(httpd_req_t* req) {
    DIR* d = opendir("/sdcard/inbox");
    if (!d) return send_text(req, "(no /sdcard/inbox)\n", "text/plain");
    char buf[1024]; size_t u = 0;
    struct dirent* e;
    while ((e = readdir(d)) && u + 64 < sizeof buf) {
        int k = snprintf(buf + u, sizeof buf - u, "%s\n", e->d_name);
        if (k > 0) u += (size_t)k;
    }
    closedir(d);
    buf[u] = 0;
    return send_text(req, buf, "text/plain");
}

bool rs_vulcan_console_start(void) {
    if (s_httpd) return true;
    if (!wifi_sta_or_ap()) {
        ESP_LOGE(TAG, "wifi failed");
        return false;
    }
    fill_ip();
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 12;
    cfg.stack_size = 8192;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return false;
    }
    const httpd_uri_t uris[] = {
        { .uri="/",           .method=HTTP_GET,  .handler=h_index },
        { .uri="/status",     .method=HTTP_GET,  .handler=h_status },
        { .uri="/run",        .method=HTTP_POST, .handler=h_run },
        { .uri="/vul",        .method=HTTP_POST, .handler=h_vul },
        { .uri="/wave",       .method=HTTP_POST, .handler=h_wave },
        { .uri="/wave/stop",  .method=HTTP_POST, .handler=h_wave_stop },
        { .uri="/cmd",        .method=HTTP_POST, .handler=h_cmd },
        { .uri="/scope.json", .method=HTTP_GET,  .handler=h_scope },
        { .uri="/ls",         .method=HTTP_GET,  .handler=h_ls },
    };
    for (size_t i = 0; i < sizeof uris / sizeof uris[0]; i++)
        httpd_register_uri_handler(s_httpd, &uris[i]);
    s_up = true;
    ESP_LOGI(TAG, "console http://%s/  role=%s", s_ip, boot_role_name(boot_role_resolve()));
    return true;
}

bool rs_vulcan_console_up(void) { return s_up; }

static void boot_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(2500));
    rs_vulcan_console_start();
    vTaskDelete(NULL);
}

void rs_vulcan_console_boot(void) {
    static bool once;
    if (once) return;
    once = true;
    xTaskCreate(boot_task, "vul_http", 6144, NULL, 4, NULL);
}
