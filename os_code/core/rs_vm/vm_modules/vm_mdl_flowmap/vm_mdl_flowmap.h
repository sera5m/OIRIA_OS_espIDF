#pragma once
// =============================================================================
// vm_mdl_flowmap – FlowMap Route { x = y; … } data-routing DSL
// At compile time routes are stored; at runtime OP_FLOW can apply them.
// A pipe filter that only renames a slot uses rsvm_flow_apply. A filter that
// changes bytes is a tph `fn … in[] out[]`, not this module.
// =============================================================================
#include "os_code/core/rs_vm/vm/rs_vm.hpp"

#ifdef __cplusplus
extern "C" {
#endif

extern const rsvm_mdl_vtbl_t rsvm_mdl_flowmap;

void rsvm_mdl_flowmap_init(rsvm_t* vm);

// Apply a single route by index (src slot → dst slot)
int rsvm_flow_apply(rsvm_t* vm, uint8_t route_id);

#ifdef __cplusplus
}
#endif
