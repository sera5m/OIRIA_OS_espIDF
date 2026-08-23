#pragma once
// =============================================================================
// vm_mdl_immut – @immut / ~immut property module
// Restricts writes to external (non-local) slots when a function is marked
// @immut. On single-threaded / desktop builds this is a soft check.
// =============================================================================
#include "os_code/core/rs_vm/vm/rs_vm.hpp"

#ifdef __cplusplus
extern "C" {
#endif

extern const rsvm_mdl_vtbl_t rsvm_mdl_immut;

void rsvm_mdl_immut_init(rsvm_t* vm);

#ifdef __cplusplus
}
#endif
