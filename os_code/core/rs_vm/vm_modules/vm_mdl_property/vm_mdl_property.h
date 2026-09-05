#pragma once
#include "os_code/core/rs_vm/vm/rs_vm.hpp"
#ifdef __cplusplus
extern "C" {
#endif
#ifndef RSVM_PROP_LATEX_INT
#define RSVM_PROP_LATEX_INT  ((uint16_t)(1u << 14))
#endif
extern const rsvm_mdl_vtbl_t rsvm_mdl_property;
void rsvm_mdl_property_init(rsvm_t* vm);
uint16_t rsvm_prop_from_name(const char* name);
#ifdef __cplusplus
}
#endif
