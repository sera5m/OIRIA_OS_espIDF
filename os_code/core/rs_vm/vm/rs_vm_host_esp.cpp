// ESP-IDF host: GPIO, SD (/sdcard), env, boot role, rpool files, UART, shell-ish.
#include "rs_vm.hpp"
#include "rs_vm_nseq.h"
#include "rs_vm_parse.hpp"
#include "rs_vm_sysconf_esp.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "os_code/core/com/boot_role.hpp"
#include "os_code/core/com/rs_collective_uart.h"
#include "os_code/core/window_env/rs_dom_link.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "code_stuff/signal_gen/siggen_play.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

static const char* TAG = "rs_vm";
static char s_cwd[160] = "/sdcard";

extern "C" int rsvm_host_siggen_native(const char* name, const int32_t* args, int nargs,
                                       int32_t* out);
extern "C" int rsvm_host_siggen_nid(int nid, const int32_t* args, int nargs,
                                    int32_t* out);

static void join_path(char* out, size_t cap, const char* p) {
    if (!p || !p[0]) { strncpy(out, s_cwd, cap - 1); out[cap-1]=0; return; }
    if (p[0] == '/') { strncpy(out, p, cap - 1); out[cap-1]=0; return; }
    snprintf(out, cap, "%s/%s", s_cwd, p);
}

static void host_print_i32(int32_t v, void*) {
    ESP_LOGI(TAG, "%ld", (long)v);
}
static void host_print_str(const char* s, uint8_t len, void*) {
    char tmp[97];
    if (len > 96) len = 96;
    memcpy(tmp, s, len);
    tmp[len] = 0;
    ESP_LOGI(TAG, "%s", tmp);
}
static void host_print_char(char c, void*) {
    ESP_LOGI(TAG, "%c", c);
}
static void host_delay_ms(uint32_t ms, void*) {
    vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}
static void host_pin_mode(uint8_t pin, uint8_t mode, void*) {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << pin;
    io.intr_type = GPIO_INTR_DISABLE;
    if (mode == 1) {
        io.mode = GPIO_MODE_OUTPUT;
    } else if (mode == 2) {
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
    } else {
        io.mode = GPIO_MODE_INPUT;
    }
    gpio_config(&io);
}
static void host_dig_write(uint8_t pin, uint8_t level, void*) {
    gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
}
static int host_dig_read(uint8_t pin, void*) {
    return gpio_get_level((gpio_num_t)pin);
}
static int host_adc_read(uint8_t pin, void*) {
    return siggen_adc_once((int)pin);
}
static uint32_t host_millis(void*) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static int host_file_read(const char* path, char* buf, int max_len, void*) {
    char full[192];
    join_path(full, sizeof full, path);
    FILE* f = fopen(full, "rb");
    if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)max_len, f);
    fclose(f);
    return n;
}
static int host_file_write(const char* path, const char* data, int len, void*) {
    char full[192];
    join_path(full, sizeof full, path);
    FILE* f = fopen(full, "wb");
    if (!f) return -1;
    int n = (int)fwrite(data, 1, (size_t)len, f);
    fclose(f);
    return n;
}

static int host_sys_ls(const char* path, char* out, int out_max, void*) {
    char full[192];
    join_path(full, sizeof full, path && path[0] ? path : ".");
    DIR* d = opendir(full);
    if (!d) return -1;
    int used = 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL && used + 2 < out_max) {
        int n = snprintf(out + used, out_max - used, "%s\n", e->d_name);
        if (n < 0) break;
        used += n;
    }
    closedir(d);
    return used;
}
static int host_sys_cd(const char* path, void*) {
    char full[192];
    join_path(full, sizeof full, path);
    struct stat st;
    if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
    strncpy(s_cwd, full, sizeof s_cwd - 1);
    return 0;
}
static int host_sys_pwd(char* out, int out_max, void*) {
    if (!out || out_max <= 0) return -1;
    strncpy(out, s_cwd, out_max - 1);
    out[out_max - 1] = 0;
    return (int)strlen(out);
}

static int host_sys_cmd(const char* cmd, char* out, int out_max, void* user) {
    if (!cmd || !out || out_max <= 0) return -1;
    out[0] = 0;
    if (!strcmp(cmd, "help") || !strncmp(cmd, "help ", 5)) {
        return snprintf(out, out_max,
            "ls cd pwd cat mkdir rm role env help run open_app wave scope stop\n");
    }
    if (!strcmp(cmd, "pwd")) return host_sys_pwd(out, out_max, user);
    if (!strncmp(cmd, "ls", 2) && (cmd[2]==0 || cmd[2]==' ')) {
        const char* p = cmd[2] ? cmd + 3 : ".";
        while (*p==' ') p++;
        return host_sys_ls(p, out, out_max, user);
    }
    if (!strncmp(cmd, "cd ", 3)) {
        int r = host_sys_cd(cmd + 3, user);
        return r == 0 ? host_sys_pwd(out, out_max, user) : -1;
    }
    if (!strncmp(cmd, "cat ", 4)) {
        return host_file_read(cmd + 4, out, out_max, user);
    }
    if (!strncmp(cmd, "mkdir ", 6)) {
        char full[192];
        join_path(full, sizeof full, cmd + 6);
        return mkdir(full, 0775) == 0 ? 0 : -1;
    }
    if (!strncmp(cmd, "rm ", 3)) {
        char full[192];
        join_path(full, sizeof full, cmd + 3);
        return unlink(full) == 0 ? 0 : -1;
    }
    if (!strcmp(cmd, "role")) {
        return snprintf(out, out_max, "%s", boot_role_name(boot_role_resolve()));
    }
    if (!strcmp(cmd, "env") || !strncmp(cmd, "env ", 4)) {
        return snprintf(out, out_max, "ROLE=%s cwd=%s",
                        boot_role_name(boot_role_resolve()), s_cwd);
    }
    if (!strncmp(cmd, "wave ", 5) || !strcmp(cmd, "wave") ||
        !strncmp(cmd, "scope", 5) || !strcmp(cmd, "stop") ||
        cmd[0]=='s' || cmd[0]=='r' || cmd[0]=='t' || cmd[0]=='p' || cmd[0]=='a' ||
        cmd[0]=='.') {
        return siggen_corz_cmd(!strncmp(cmd, "wave ", 5) ? cmd + 5 : cmd, out, out_max);
    }
    snprintf(out, out_max, "unknown cmd");
    return -1;
}

static int host_sys_exec(const char* path, void*) {
    char full[192];
    join_path(full, sizeof full, path);
    rsvm_t* vm = (rsvm_t*)malloc(sizeof(rsvm_t));
    if (!vm) return -1;
    rsvm_init(vm);
    rsvm_install_esp_host(vm);
    rsvm_parse_err_t err{};
    rsvm_status_t st = rsvm_eval_file(vm, full, &err);
    if (st != RSVM_OK)
        ESP_LOGE(TAG, "exec %s L%d %s", full, err.line, err.message);
    free(vm);
    return st == RSVM_OK ? 0 : (int)st;
}

static int host_sys_open_app(const char* name, void*) {
    if (!name || !name[0]) return -1;
    appManager::instance().close_current_and_open(name);
    return 0;
}

static int host_mw_set_text(int32_t win_id, const char* text, void*) {
    if (!text) return -1;
    auto& wm = WindowManager::getInstance();
    (void)win_id;
    wm.SetToolbarText(text);
    return 0;
}

static int host_uart_send(uint8_t type, const uint8_t* data, uint16_t len, void*) {
    return (int)rs_dom_link_send(type, data, len);
}

static int host_pool_op(const char* name, int op, const char* data, int len,
                        char* out, int out_max, void*) {
    if (!name || !name[0]) return -1;
    char path[192];
    snprintf(path, sizeof path, "/sdcard/rpool/%s.rpool", name);
    if (op == 3) { /* delete */
        return unlink(path) == 0 ? 0 : -1;
    }
    if (op == 2) { /* exists */
        struct stat st;
        return stat(path, &st) == 0 ? 1 : 0;
    }
    if (op == 1) { /* put */
        mkdir("/sdcard/rpool", 0775);
        FILE* f = fopen(path, "wb");
        if (!f) return -1;
        int n = data && len > 0 ? (int)fwrite(data, 1, (size_t)len, f) : 0;
        fclose(f);
        return n;
    }
    /* get */
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    int n = out && out_max > 0 ? (int)fread(out, 1, (size_t)out_max, f) : 0;
    fclose(f);
    return n;
}

static int host_sys_env(const char* key, char* out, int out_max, void*) {
    if (!key || !out) return -1;
    if (!strcmp(key, "ROLE") || !strcmp(key, "boot_role"))
        return snprintf(out, out_max, "%s", boot_role_name(boot_role_resolve()));
    if (!strcmp(key, "PWD") || !strcmp(key, "cwd"))
        return snprintf(out, out_max, "%s", s_cwd);
    if (!strcmp(key, "PUPPET"))
        return snprintf(out, out_max, "%d", rs_coll_uart_conf()->is_puppet);
    return rsvm_sysconf_get_venv(key, out, out_max);
}

static int host_native(const char* name, const int32_t* args, int nargs,
                       int32_t* out, void*) {
    int r = rsvm_host_siggen_native(name, args, nargs, out);
    if (r != -2) return r;
    return -1;
}

static int host_native_id(int nid, const int32_t* args, int nargs,
                          int32_t* out, void*) {
    return rsvm_host_siggen_nid(nid, args, nargs, out);
}

static int nseq_step(int nid, const int32_t* a, int na, int32_t* out, void*) {
    if (out) *out = 0;
    int32_t d0 = (na > 0 && a) ? a[0] : 0;
    int32_t d1 = (na > 1 && a) ? a[1] : 0;
    switch (nid) {
    case RSVM_NID_NOP:
        return 0;
    case RSVM_NID_DELAY:
        host_delay_ms((uint32_t)(d0 < 0 ? 0 : d0), NULL);
        return 0;
    case RSVM_NID_PIN_MODE:
        host_pin_mode((uint8_t)d0, (uint8_t)d1, NULL);
        return 0;
    case RSVM_NID_GPIO_WR:
    case RSVM_NID_DIG_WR:
        host_dig_write((uint8_t)d0, (uint8_t)d1, NULL);
        return 0;
    case RSVM_NID_GPIO_RD:
    case RSVM_NID_DIG_RD:
        if (out) *out = host_dig_read((uint8_t)d0, NULL);
        return 0;
    case RSVM_NID_ADC:
        if (out) *out = host_adc_read((uint8_t)d0, NULL);
        return 0;
    default:
        return rsvm_host_siggen_nid(nid, a, na, out);
    }
}

extern "C" int rsvm_esp_nseq_exec(const rsvm_nstep_t* steps, int nsteps, int32_t* out) {
    return rsvm_nseq_run(steps, nsteps, nseq_step, NULL, out);
}

static int host_nseq_run(const void* steps, int nsteps, int32_t* out, void* user) {
    const rsvm_nstep_t* s = (const rsvm_nstep_t*)steps;
    /* One blob. Not N sprintf-of-source translates. */
    if (boot_role_resolve() == BOOT_ROLE_TYRANT) {
        uint8_t blob[8 + sizeof(rsvm_nstep_t) * RSVM_NSEQ_MAX];
        size_t n = rsvm_nseq_pack(s, nsteps, blob, sizeof blob);
        if (n) rs_dom_link_send(RSDOM_TYPE_NSEQ, blob, (uint16_t)n);
    }
    (void)user;
    return rsvm_esp_nseq_exec(s, nsteps, out);
}

static int host_sysconf_get(const char* key, char* out, int out_max, void*) {
    return rsvm_sysconf_get_venv(key, out, out_max);
}
static int host_sysconf_set(const char* key, const char* value, void*) {
    return rsvm_sysconf_set_venv(key, value);
}

extern "C" void rsvm_install_esp_host(rsvm_t* vm) {
    if (!vm) return;
    rsvm_host_t h = {};
    h.print_i32   = host_print_i32;
    h.print_str   = host_print_str;
    h.print_char  = host_print_char;
    h.delay_ms    = host_delay_ms;
    h.pin_mode    = host_pin_mode;
    h.dig_write   = host_dig_write;
    h.dig_read    = host_dig_read;
    h.adc_read    = host_adc_read;
    h.millis      = host_millis;
    h.file_read   = host_file_read;
    h.file_write  = host_file_write;
    h.sys_cmd     = host_sys_cmd;
    h.sys_exec    = host_sys_exec;
    h.sys_open_app= host_sys_open_app;
    h.sys_ls      = host_sys_ls;
    h.sys_cd      = host_sys_cd;
    h.sys_pwd     = host_sys_pwd;
    h.mw_set_text = host_mw_set_text;
    h.uart_send   = host_uart_send;
    h.pool_op     = host_pool_op;
    h.sys_env     = host_sys_env;
    h.sysconf_get = host_sysconf_get;
    h.sysconf_set = host_sysconf_set;
    h.native_call = host_native;
    h.native_id   = host_native_id;
    h.nseq_run    = host_nseq_run;
    rsvm_set_host(vm, &h);
}
