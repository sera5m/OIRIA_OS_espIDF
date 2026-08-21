// Drop-in helpers for rs_vm_host_esp.cpp — map keys ↔ v_env (EnvConfig)
// Requires env_vars.h (extern EnvConfig v_env).
#pragma once
#include "os_code/core/rShell/enviroment/env_vars.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int rsvm_sysconf_get_venv(const char* key, char* out, int out_max) {
    if (!key || !out || out_max <= 0) return -1;
#define YN(b) ((b) ? 1 : 0)
    if (!strcmp(key, "brightness"))        snprintf(out, out_max, "%u", (unsigned)v_env.brightness);
    else if (!strcmp(key, "fpsTarget"))    snprintf(out, out_max, "%u", (unsigned)v_env.fpsTarget);
    else if (!strcmp(key, "headless"))     snprintf(out, out_max, "%d", YN(v_env.headless));
    else if (!strcmp(key, "debugMode"))    snprintf(out, out_max, "%d", YN(v_env.debugMode));
    else if (!strcmp(key, "safeMode"))     snprintf(out, out_max, "%d", YN(v_env.safeMode));
    else if (!strcmp(key, "hasMicroSD"))   snprintf(out, out_max, "%d", YN(v_env.hasMicroSD));
    else if (!strcmp(key, "screen_dim_w")) snprintf(out, out_max, "%u", (unsigned)v_env.screen_dim_w);
    else if (!strcmp(key, "screen_dim_h")) snprintf(out, out_max, "%u", (unsigned)v_env.screen_dim_h);
    else if (!strcmp(key, "batteryPercent")) snprintf(out, out_max, "%u", (unsigned)v_env.batteryPercent);
    else if (!strcmp(key, "charging"))     snprintf(out, out_max, "%d", YN(v_env.charging));
    else if (!strcmp(key, "cpuMhzTarget")) snprintf(out, out_max, "%u", (unsigned)v_env.cpuMhzTarget);
    else if (!strcmp(key, "bootCount"))    snprintf(out, out_max, "%lu", (unsigned long)v_env.bootCount);
    else return -1;
    return (int)strlen(out);
#undef YN
}

static inline int rsvm_sysconf_set_venv(const char* key, const char* value) {
    if (!key || !value) return -1;
    int v = atoi(value);
    if (!strcmp(key, "brightness"))        v_env.brightness = (uint8_t)v;
    else if (!strcmp(key, "fpsTarget"))    v_env.fpsTarget = (uint16_t)v;
    else if (!strcmp(key, "headless"))     v_env.headless = (v != 0);
    else if (!strcmp(key, "debugMode"))    v_env.debugMode = (v != 0);
    else if (!strcmp(key, "safeMode"))     v_env.safeMode = (v != 0);
    else if (!strcmp(key, "screen_dim_w")) v_env.screen_dim_w = (uint16_t)v;
    else if (!strcmp(key, "screen_dim_h")) v_env.screen_dim_h = (uint16_t)v;
    else if (!strcmp(key, "cpuMhzTarget")) v_env.cpuMhzTarget = (uint16_t)v;
    else return -1;
    return 0;
}
