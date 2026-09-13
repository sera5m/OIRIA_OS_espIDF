// native("wave" | "wave_stop" | …) and interned-nid fast path (no UART translate).
#include "rs_vm.hpp"
#include "rs_vm_nseq.h"
#include "code_stuff/signal_gen/siggen_play.h"
#include "os_code/core/com/boot_role.hpp"
#include "os_code/core/window_env/rs_dom_link.hpp"
#include <string.h>
#include <stdio.h>

/* Hardware only. Tyrant UART-forward lives on the *name* path and on nseq_run. */
extern "C" int rsvm_host_siggen_nid(int nid, const int32_t* args, int nargs,
                                    int32_t* out) {
    auto a = [&](int i, int32_t d = 0) -> int32_t {
        return (i < nargs && args) ? args[i] : d;
    };
    if (out) *out = 0;
    switch (nid) {
    case RSVM_NID_WAVE: {
        int kind = a(0, 0);
        int freq = a(1, 1000);
        int duty = a(2, 50);
        int pin  = a(3, SIGGEN_DEFAULT_OUT_GPIO);
        int amp  = a(4, 80);
        int e = (int)siggen_play(kind, (uint32_t)freq, (uint8_t)duty, pin, (uint8_t)amp);
        if (out) *out = e;
        return e == 0 ? 0 : -1;
    }
    case RSVM_NID_WAVE_STOP:
        siggen_play_stop();
        return 0;
    case RSVM_NID_WAVE_FREQ: {
        int e = (int)siggen_play_set_freq((uint32_t)a(0, 1000));
        if (out) *out = e;
        return e == 0 ? 0 : -1;
    }
    case RSVM_NID_WAVE_DUTY: {
        int e = (int)siggen_play_set_duty((uint8_t)a(0, 50));
        if (out) *out = e;
        return e == 0 ? 0 : -1;
    }
    case RSVM_NID_WAVE_AMP: {
        int e = (int)siggen_play_set_amp((uint8_t)a(0, 80));
        if (out) *out = e;
        return e == 0 ? 0 : -1;
    }
    case RSVM_NID_SWEEP: {
        int e = (int)siggen_play_sweep((uint32_t)a(0, 100), (uint32_t)a(1, 2000),
                                       (uint32_t)a(2, 2000));
        if (out) *out = e;
        return e == 0 ? 0 : -1;
    }
    case RSVM_NID_ADC: {
        int mv = siggen_adc_once(a(0, SIGGEN_DEFAULT_SCOPE_GPIO));
        if (out) *out = mv;
        return mv < 0 ? -1 : 0;
    }
    case RSVM_NID_SCOPE: {
        int16_t buf[64];
        int n = a(1, 1);
        if (n > 64) n = 64;
        int got = siggen_scope_cap(a(0, SIGGEN_DEFAULT_SCOPE_GPIO), n, buf, NULL);
        if (out) *out = (got > 0) ? buf[got - 1] : got;
        return got < 0 ? -1 : 0;
    }
    default:
        return -2;
    }
}

extern "C" int rsvm_host_siggen_native(const char* name, const int32_t* args, int nargs,
                                       int32_t* out) {
    if (!name) return -1;
    int nid = rsvm_nid_from_name(name);
    if (nid < 0) {
        if (!strcmp(name, "wave_gpio")) {
            if (out) *out = siggen_play_gpio();
            return 0;
        }
        return -2;
    }
    int r = rsvm_host_siggen_nid(nid, args, nargs, out);
    if (r == -2) return -2;
    /* Single native() still translates for the puppet (one sprintf). */
    if (boot_role_resolve() == BOOT_ROLE_TYRANT) {
        if (nid == RSVM_NID_WAVE) {
            char buf[128];
            auto a = [&](int i, int32_t d = 0) -> int32_t {
                return (i < nargs && args) ? args[i] : d;
            };
            snprintf(buf, sizeof buf,
                     "native(\"wave\", %d, %d, %d, %d, %d);\n",
                     (int)a(0, 0), (int)a(1, 1000), (int)a(2, 50),
                     (int)a(3, SIGGEN_DEFAULT_OUT_GPIO), (int)a(4, 80));
            rs_dom_link_send(RSDOM_TYPE_VM_SRC, (const uint8_t*)buf,
                             (uint16_t)strlen(buf));
        } else if (nid == RSVM_NID_WAVE_STOP) {
            rs_dom_link_send(RSDOM_TYPE_VM_SRC,
                             (const uint8_t*)"native(\"wave_stop\");\n", 22);
        }
    }
    return r;
}
