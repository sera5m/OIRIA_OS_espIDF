// vm_mdl_thread – sequential fallback + config for FreeRTOS later
#include "os_code/core/rs_vm/vm_modules/vm_mdl_thread/vm_mdl_thread.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void on_enter(rsvm_t* vm, uint8_t func_id) {
    if (!vm || func_id >= vm->func_count) return;
    if (!(vm->funcs[func_id].props & RSVM_PROP_THREADED)) return;
    // reset per-call desired thread count
    vm->thread_desired = 1;
}

static void on_exit(rsvm_t* vm, uint8_t func_id) {
    (void)vm; (void)func_id;
}

static void on_store(rsvm_t* vm, uint8_t slot) {
    (void)vm; (void)slot;
}

static int configure(rsvm_t* vm, const char* key, const char* val) {
    if (!vm || !key) return -1;
    if (strcmp(key, "ifsingle") == 0) {
        vm->thread_ifsingle = (val && (val[0]=='1' || val[0]=='t' || val[0]=='T'));
        return 0;
    }
    if (strcmp(key, "thread_sequencing") == 0) {
        vm->thread_sequencing = (val && (val[0]=='1' || val[0]=='t' || val[0]=='T'));
        return 0;
    }
    if (strcmp(key, "desired_threads") == 0 || strcmp(key, "threadcount") == 0) {
        if (val) vm->thread_desired = (uint8_t)atoi(val);
        return 0;
    }
    return -1;
}

const rsvm_mdl_vtbl_t rsvm_mdl_thread = {
    "thread",
    on_enter,
    on_exit,
    on_store,
    configure,
};

void rsvm_mdl_thread_init(rsvm_t* vm) {
    if (!vm) return;
    vm->mdl_thread = &rsvm_mdl_thread;
    vm->thread_ifsingle = true;      // desktop default: sequential
    vm->thread_sequencing = true;
    vm->thread_desired = 1;
}
