// vm_mdl_property – name ↔ bitflag map and generic hooks
#include "vm_mdl_property.h"
#include <string.h>

uint16_t rsvm_prop_from_name(const char* name) {
    if (!name) return RSVM_PROP_NONE;
    if (strcmp(name, "immut") == 0)         return RSVM_PROP_IMMUT;
    if (strcmp(name, "threaded") == 0)      return RSVM_PROP_THREADED;
    if (strcmp(name, "property") == 0)      return RSVM_PROP_PROPERTY;
    if (strcmp(name, "shared") == 0)        return RSVM_PROP_SHARED;
    if (strcmp(name, "mut") == 0)           return RSVM_PROP_MUT;
    if (strcmp(name, "ignore") == 0)        return RSVM_PROP_IGNORE;
    if (strcmp(name, "monitor_time") == 0)  return RSVM_PROP_MON_TIME;
    if (strcmp(name, "monitor_ram") == 0)   return RSVM_PROP_MON_RAM;
    if (strcmp(name, "monitor_execs") == 0) return RSVM_PROP_MON_EXECS;
    if (strcmp(name, "monitor_time_highres") == 0) return RSVM_PROP_MON_TIME_HI;
    if (strcmp(name, "loops_trapdoor") == 0) return RSVM_PROP_LOOPS_TRAP;
    if (strcmp(name, "autounroll") == 0)     return RSVM_PROP_AUTOUNROLL;
    if (strcmp(name, "unlimited") == 0)    return RSVM_PROP_UNLIMITED;
    if (strcmp(name, "latex_internal") == 0) return RSVM_PROP_LATEX_INT;
    if (strcmp(name, "memory_hard") == 0)   return RSVM_PROP_MEMORY_HARD;
    return RSVM_PROP_NONE;
}

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
const rsvm_mdl_vtbl_t rsvm_mdl_property = {
    "property", on_enter, on_exit, on_store, configure,
};
void rsvm_mdl_property_init(rsvm_t* vm) {
    if (vm) vm->mdl_property = &rsvm_mdl_property;
}
