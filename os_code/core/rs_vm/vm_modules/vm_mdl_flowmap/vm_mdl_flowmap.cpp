// vm_mdl_flowmap – simple rename / data-flow routes
#include "os_code/core/rs_vm/vm_modules/vm_mdl_flowmap/vm_mdl_flowmap.h"
#include <string.h>

static void on_enter(rsvm_t* vm, uint8_t func_id) {
    (void)vm; (void)func_id;
}

static void on_exit(rsvm_t* vm, uint8_t func_id) {
    (void)vm; (void)func_id;
}

static void on_store(rsvm_t* vm, uint8_t slot) {
    (void)vm; (void)slot;
}

static int configure(rsvm_t* vm, const char* key, const char* val) {
    (void)vm; (void)key; (void)val;
    return 0;
}

const rsvm_mdl_vtbl_t rsvm_mdl_flowmap = {
    "flowmap",
    on_enter,
    on_exit,
    on_store,
    configure,
};

void rsvm_mdl_flowmap_init(rsvm_t* vm) {
    if (vm) vm->mdl_flowmap = &rsvm_mdl_flowmap;
}

int rsvm_flow_apply(rsvm_t* vm, uint8_t route_id) {
    if (!vm || route_id >= vm->flow_count) return -1;
    const rsvm_flow_route_t* r = &vm->flows[route_id];
    uint8_t src = rsvm_slot_by_name(vm, r->src, false);
    uint8_t dst = rsvm_slot_by_name(vm, r->dst, true);
    if (src == 0xFF || dst == 0xFF) return -1;
    vm->slots[dst] = vm->slots[src];
    return 0;
}
