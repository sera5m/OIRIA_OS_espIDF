// vm_mdl_immut – enforces @immut functions cannot write external slots
#include "vm_mdl_immut.h"
#include <stdio.h>
#include <string.h>

static void on_enter(rsvm_t* vm, uint8_t func_id) {
    (void)vm; (void)func_id;
    // future: snapshot writable local range
}

static void on_exit(rsvm_t* vm, uint8_t func_id) {
    (void)vm; (void)func_id;
}

static void on_store(rsvm_t* vm, uint8_t slot) {
    if (!vm || vm->csp < 0) return;
    uint8_t fid = vm->calls[vm->csp].func_id;
    if (fid >= vm->func_count) return;
    if (!(vm->funcs[fid].props & RSVM_PROP_IMMUT)) return;

    // In @immut context: only allow stores into the function's own arg/out
    // slots or slots created after the call (locals). For the light VM we
    // currently only flag the violation; full enforcement can raise ERR_PROP.
    const rsvm_func_ent_t* f = &vm->funcs[fid];
    bool local = false;
    for (uint8_t i = 0; i < f->n_in; ++i)
        if (f->arg_slots[i] == slot) { local = true; break; }
    for (uint8_t i = 0; i < f->n_out && !local; ++i)
        if (f->out_slots[i] == slot) { local = true; break; }
    if (!local) {
        // Soft: print once on desktop; on ESP could set last_status
#if !defined(ESP_PLATFORM)
        static int warned;
        if (warned < 3) {
            fprintf(stderr, "[immut] write to external slot %u in %s\n",
                    (unsigned)slot, f->name);
            warned++;
        }
#endif
        // vm->last_status = RSVM_ERR_PROP;  // strict mode
    }
}

static int configure(rsvm_t* vm, const char* key, const char* val) {
    (void)vm; (void)key; (void)val;
    return 0;
}

const rsvm_mdl_vtbl_t rsvm_mdl_immut = {
    "immut",
    on_enter,
    on_exit,
    on_store,
    configure,
};

void rsvm_mdl_immut_init(rsvm_t* vm) {
    if (vm) vm->mdl_immut = &rsvm_mdl_immut;
}
