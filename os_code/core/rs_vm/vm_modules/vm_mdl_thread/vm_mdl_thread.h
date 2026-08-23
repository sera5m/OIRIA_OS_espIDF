#pragma once
// =============================================================================
// vm_mdl_thread – @threaded property module
//
// Syntax inside an @threaded function:
//   ```key=val```          configure submodule (ifsingle, thread_sequencing …)
//   `thread_0 { … }`       sequential / ordered block
//   ``thread_n { … }``     embarrassingly parallel (spawn unlimited if HW allows)
//
// On hosts without true multi-threading (desktop test, or ESP when
// ```ifsingle=true```) blocks run in source order.
// =============================================================================
#include "os_code/core/rs_vm/vm/rs_vm.hpp"

#ifdef __cplusplus
extern "C" {
#endif

extern const rsvm_mdl_vtbl_t rsvm_mdl_thread;

void rsvm_mdl_thread_init(rsvm_t* vm);

#ifdef __cplusplus
}
#endif
