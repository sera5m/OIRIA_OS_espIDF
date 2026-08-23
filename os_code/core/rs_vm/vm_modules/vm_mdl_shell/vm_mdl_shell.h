#pragma once
// Bash-like OS integration surface for Vulcan (host-backed).
#include "os_code/core/rs_vm/vm/rs_vm.hpp"
#ifdef __cplusplus
extern "C" {
#endif
extern const rsvm_mdl_vtbl_t rsvm_mdl_shell;
void rsvm_mdl_shell_init(rsvm_t* vm);
#ifdef __cplusplus
}
#endif
