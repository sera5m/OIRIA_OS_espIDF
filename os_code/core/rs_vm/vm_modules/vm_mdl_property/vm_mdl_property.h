#pragma once
// =============================================================================
// vm_mdl_property – generic @property / @shared / @mut module
// Tracks per-slot property flags and provides a central registry of the
// property enum used by the VM.
// =============================================================================
#include "rs_vm.hpp"

#ifdef __cplusplus
extern "C" {
#endif

extern const rsvm_mdl_vtbl_t rsvm_mdl_property;

void rsvm_mdl_property_init(rsvm_t* vm);

// Map property name → bitflag (returns 0 if unknown)
uint16_t rsvm_prop_from_name(const char* name);

#ifdef __cplusplus
}
#endif
