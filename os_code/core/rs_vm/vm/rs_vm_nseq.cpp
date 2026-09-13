#include "rs_vm_nseq.h"
#include <string.h>

static const char* const k_names[RSVM_NID_COUNT] = {
    "nop",
    "wave",
    "wave_stop",
    "wave_freq",
    "wave_duty",
    "wave_amp",
    "sweep",
    "adc",
    "scope",
    "delay",
    "gpio_wr",
    "gpio_rd",
    "pin_mode",
    "dig_wr",
    "dig_rd",
    "corz",
};

int rsvm_nid_from_name(const char* name) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < RSVM_NID_COUNT; ++i) {
        if (strcmp(name, k_names[i]) == 0) return i;
    }
    if (!strcmp(name, "siggen_start")) return RSVM_NID_WAVE;
    if (!strcmp(name, "siggen_stop"))  return RSVM_NID_WAVE_STOP;
    if (!strcmp(name, "scope_mv"))     return RSVM_NID_ADC;
    if (!strcmp(name, "sleep") || !strcmp(name, "wait")) return RSVM_NID_DELAY;
    if (!strcmp(name, "gpio_write"))   return RSVM_NID_GPIO_WR;
    if (!strcmp(name, "gpio_read"))    return RSVM_NID_GPIO_RD;
    return -1;
}

const char* rsvm_nid_name(int nid) {
    if (nid < 0 || nid >= RSVM_NID_COUNT) return "nop";
    return k_names[nid];
}

int rsvm_nseq_run(const rsvm_nstep_t* steps, int nsteps,
                  rsvm_nseq_host_fn host, void* user, int32_t* last_out) {
    int32_t last = 0;
    if (nsteps < 0) nsteps = 0;
    if (nsteps > RSVM_NSEQ_MAX) nsteps = RSVM_NSEQ_MAX;
    for (int i = 0; i < nsteps; ++i) {
        int nid = (int)steps[i].nid;
        int na  = (int)steps[i].nargs;
        if (na < 0) na = 0;
        if (na > RSVM_NSEQ_MAXARG) na = RSVM_NSEQ_MAXARG;
        int32_t out = 0;
        if (host) host(nid, steps[i].args, na, &out, user);
        last = out;
    }
    if (last_out) *last_out = last;
    return 0;
}

size_t rsvm_nseq_pack(const rsvm_nstep_t* steps, int nsteps,
                      uint8_t* out, size_t cap) {
    if (!out) return 0;
    if (nsteps < 0) nsteps = 0;
    if (nsteps > RSVM_NSEQ_MAX) nsteps = RSVM_NSEQ_MAX;
    size_t need = 8u + (size_t)nsteps * sizeof(rsvm_nstep_t);
    if (need > cap) return 0;
    out[0] = RSVM_NSEQ_MAGIC0;
    out[1] = RSVM_NSEQ_MAGIC1;
    out[2] = RSVM_NSEQ_MAGIC2;
    out[3] = RSVM_NSEQ_MAGIC3;
    out[4] = (uint8_t)nsteps;
    out[5] = 0;
    out[6] = 0;
    out[7] = 0;
    if (nsteps && steps)
        memcpy(out + 8, steps, (size_t)nsteps * sizeof(rsvm_nstep_t));
    return need;
}

int rsvm_nseq_unpack(const uint8_t* blob, size_t len,
                     rsvm_nstep_t* steps, int max_steps) {
    if (!blob || len < 8) return -1;
    if (blob[0] != RSVM_NSEQ_MAGIC0 || blob[1] != RSVM_NSEQ_MAGIC1 ||
        blob[2] != RSVM_NSEQ_MAGIC2 || blob[3] != RSVM_NSEQ_MAGIC3)
        return -1;
    int n = (int)blob[4];
    if (n < 0) n = 0;
    if (n > RSVM_NSEQ_MAX) n = RSVM_NSEQ_MAX;
    if (max_steps > 0 && n > max_steps) n = max_steps;
    size_t need = 8u + (size_t)n * sizeof(rsvm_nstep_t);
    if (len < need) return -1;
    if (steps && n)
        memcpy(steps, blob + 8, (size_t)n * sizeof(rsvm_nstep_t));
    return n;
}

void rsvm_nseq_from_i32(const int32_t* flat, int nflat,
                        rsvm_nstep_t* steps, int* nsteps_io) {
    if (!nsteps_io) return;
    int maxn = *nsteps_io;
    if (maxn > RSVM_NSEQ_MAX) maxn = RSVM_NSEQ_MAX;
    if (!flat || nflat < RSVM_NSEQ_STRIDE || !steps) {
        *nsteps_io = 0;
        return;
    }
    int n = nflat / RSVM_NSEQ_STRIDE;
    if (n > maxn) n = maxn;
    for (int i = 0; i < n; ++i) {
        const int32_t* p = flat + i * RSVM_NSEQ_STRIDE;
        steps[i].nid   = p[0];
        steps[i].nargs = p[1];
        steps[i].args[0] = p[2];
        steps[i].args[1] = p[3];
        steps[i].args[2] = p[4];
        steps[i].args[3] = p[5];
        steps[i].args[4] = p[6];
    }
    *nsteps_io = n;
}
