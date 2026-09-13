// native("wave" | "wave_stop" | "adc" | "scope" | "sweep" | "corz" ...)
#include "rs_vm.hpp"
#include "code_stuff/signal_gen/siggen_play.h"
#include "os_code/core/com/boot_role.hpp"
#include "os_code/core/window_env/rs_dom_link.hpp"
#include <string.h>
#include <stdio.h>

extern "C" int rsvm_host_siggen_native(const char* name, const int32_t* args, int nargs,
                                       int32_t* out) {
    if (!name) return -1;
    if (out) *out = 0;

    auto a = [&](int i, int32_t d = 0) -> int32_t {
        return (i < nargs && args) ? args[i] : d;
    };

    if (!strcmp(name, "wave") || !strcmp(name, "siggen_start")) {
        /* native("wave", kind, freq, duty, pin, amp)
         * kind 0 sine / 1 square / 2 tri / 3 saw / 4 noise */
        int kind = a(0, 0);
        int freq = a(1, 1000);
        int duty = a(2, 50);
        int pin  = a(3, SIGGEN_DEFAULT_OUT_GPIO);
        int amp  = a(4, 80);
        int e = (int)siggen_play(kind, (uint32_t)freq, (uint8_t)duty, pin, (uint8_t)amp);
        if (out) *out = e;
        /* Tyrant may also punch the puppet so the worker actually drives the pin. */
        if (boot_role_resolve() == BOOT_ROLE_TYRANT) {
            char buf[128];
            snprintf(buf, sizeof buf,
                     "native(\"wave\", %d, %d, %d, %d, %d);\n",
                     kind, freq, duty, pin, amp);
            rs_dom_link_send(RSDOM_TYPE_VM_SRC, (const uint8_t*)buf, (uint16_t)strlen(buf));
        }
        return e == 0 ? 0 : -1;
    }
    if (!strcmp(name, "wave_stop") || !strcmp(name, "siggen_stop")) {
        siggen_play_stop();
        if (boot_role_resolve() == BOOT_ROLE_TYRANT)
            rs_dom_link_send(RSDOM_TYPE_VM_SRC, (const uint8_t*)"native(\"wave_stop\");\n", 22);
        return 0;
    }
    if (!strcmp(name, "wave_freq")) {
        int e = (int)siggen_play_set_freq((uint32_t)a(0, 1000));
        if (out) *out = e;
        return e == 0 ? 0 : -1;
    }
    if (!strcmp(name, "wave_duty")) {
        int e = (int)siggen_play_set_duty((uint8_t)a(0, 50));
        if (out) *out = e;
        return e == 0 ? 0 : -1;
    }
    if (!strcmp(name, "wave_amp")) {
        int e = (int)siggen_play_set_amp((uint8_t)a(0, 80));
        if (out) *out = e;
        return e == 0 ? 0 : -1;
    }
    if (!strcmp(name, "sweep")) {
        /* native("sweep", f0, f1, ms) */
        int e = (int)siggen_play_sweep((uint32_t)a(0, 100), (uint32_t)a(1, 2000), (uint32_t)a(2, 2000));
        if (out) *out = e;
        return e == 0 ? 0 : -1;
    }
    if (!strcmp(name, "adc") || !strcmp(name, "scope_mv")) {
        int mv = siggen_adc_once(a(0, SIGGEN_DEFAULT_SCOPE_GPIO));
        if (out) *out = mv;
        return mv < 0 ? -1 : 0;
    }
    if (!strcmp(name, "scope")) {
        /* native("scope", pin, n) — returns sample count; last mV in *out if n==1 */
        int16_t buf[64];
        int n = a(1, 1);
        if (n > 64) n = 64;
        int got = siggen_scope_cap(a(0, SIGGEN_DEFAULT_SCOPE_GPIO), n, buf, NULL);
        if (out) *out = (got > 0) ? buf[got - 1] : got;
        return got < 0 ? -1 : 0;
    }
    if (!strcmp(name, "wave_gpio")) {
        if (out) *out = siggen_play_gpio();
        return 0;
    }
    return -2; /* not a siggen name — host may try other tables */
}
