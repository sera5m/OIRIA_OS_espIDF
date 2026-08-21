#pragma once
#include "rs_vm_opcodes.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RSVM_OK = 0,
    RSVM_ERR_BAD_MAGIC,
    RSVM_ERR_CODE,
    RSVM_ERR_STACK,
    RSVM_ERR_SLOT,
    RSVM_ERR_STEP_LIMIT,
    RSVM_ERR_DIV0,
    RSVM_ERR_HOST,
    RSVM_ERR_CALL,
    RSVM_ERR_HEAP,
    RSVM_ERR_TYPE,
    RSVM_ERR_NULL_PTR,
    RSVM_ERR_PROP,       // property / immut violation
} rsvm_status_t;

typedef struct {
    void (*print_i32)(int32_t v, void* user);
    void (*print_str)(const char* s, uint8_t len, void* user);
    void (*print_char)(char c, void* user);   // single char, no newline
    void (*delay_ms)(uint32_t ms, void* user);
    void (*pin_mode)(uint8_t pin, uint8_t mode, void* user);
    void (*dig_write)(uint8_t pin, uint8_t level, void* user);
    int  (*dig_read)(uint8_t pin, void* user);
    int  (*adc_read)(uint8_t pin, void* user);
    uint32_t (*millis)(void* user);
    // File — return bytes read/written; path is NUL-terminated C string
    // read: fill buf up to max_len, return nbytes (>=0) or -1
    int  (*file_read)(const char* path, char* buf, int max_len, void* user);
    int  (*file_write)(const char* path, const char* data, int len, void* user);
    // Shell / OS (optional — desktop stubs or ESP rShell bridges)
    // sys_cmd: run a short shell-like command; write text reply into out; return nbytes or -1
    int  (*sys_cmd)(const char* cmd, char* out, int out_max, void* user);
    int  (*sys_exec)(const char* path, void* user);           // run .vul/.bvul
    int  (*sys_open_app)(const char* name, void* user);       // appManager close_current_and_open
    int  (*sys_ls)(const char* path, char* out, int out_max, void* user);
    int  (*sys_cd)(const char* path, void* user);
    int  (*sys_pwd)(char* out, int out_max, void* user);
    int  (*mw_set_text)(int32_t win_id, const char* text, void* user);
    int  (*uart_send)(uint8_t type, const uint8_t* data, uint16_t len, void* user);
    // pool_op: op 0=get 1=put 2=exists 3=delete — name is pool/file key
    int  (*pool_op)(const char* name, int op, const char* data, int len,
                    char* out, int out_max, void* user);
    int  (*sys_env)(const char* key, char* out, int out_max, void* user);
    // v_env / EnvConfig bridge — key is field name ("brightness", "headless", …)
    // get: write string form into out; *out_is_int set if numeric (optional via out[0] digit)
    // returns nbytes or -1
    int  (*sysconf_get)(const char* key, char* out, int out_max, void* user);
    // set: value is string form of new value; return 0 on OK
    int  (*sysconf_set)(const char* key, const char* value, void* user);
    // C interop: name is C function key; args[0..nargs); write result to *out; return 0 OK
    int  (*native_call)(const char* name, const int32_t* args, int nargs,
                        int32_t* out, void* user);
    void* user;
} rsvm_host_t;

typedef struct {
    uint16_t ret_pc;
    uint8_t  fp;          // previous frame base (slot index)
    int8_t   sp_save;
    uint8_t  n_outs;
    uint8_t  func_id;
    uint32_t step_limit_save;
} rsvm_frame_t;

// Forward for module callbacks
struct rsvm_s;
typedef struct rsvm_s rsvm_t;

// Optional property module vtable (one per property family)
typedef struct {
    const char* name;
    void (*on_func_enter)(rsvm_t* vm, uint8_t func_id);
    void (*on_func_exit)(rsvm_t* vm, uint8_t func_id);
    void (*on_store)(rsvm_t* vm, uint8_t slot);   // for @immut checks etc.
    int  (*configure)(rsvm_t* vm, const char* key, const char* val); // ```key=val```
} rsvm_mdl_vtbl_t;

struct rsvm_s {
    // Code
    uint8_t  code[RSVM_MAX_CODE];
    uint16_t code_len;
    uint16_t pc;
    uint16_t entry;
    uint32_t step_limit;
    uint32_t steps;

    // Named / local slots (flat; frames use a base offset)
    rsvm_val_t slots[RSVM_MAX_SLOTS];
    uint8_t    slot_count;          // high-water of allocated names
    uint8_t    frame_base;          // current local base
    char       names[RSVM_MAX_SLOTS][RSVM_NAME_LEN];
    uint8_t    slot_props[RSVM_MAX_SLOTS];  // per-slot @shared/@mut flags

    // Operand stack
    rsvm_val_t stack[RSVM_MAX_STACK];
    int8_t     sp;

    // Call stack
    rsvm_frame_t calls[RSVM_MAX_CALLS];
    int8_t       csp;

    // Function table
    rsvm_func_ent_t funcs[RSVM_MAX_FUNCS];
    uint8_t         func_count;

    // Struct / enum tables
    rsvm_struct_ent_t structs[RSVM_MAX_STRUCTS];
    uint8_t           struct_count;
    rsvm_enum_ent_t   enums[RSVM_MAX_ENUMS];
    uint8_t           enum_count;

    // FlowMap routes
    rsvm_flow_route_t flows[RSVM_MAX_FLOW_ROUTES];
    uint8_t           flow_count;
    uint8_t           break_flag;   // 1 = break nearest trap loop
    uint8_t           cont_flag;

    // Heap arena for objects (smart pointers)
    uint8_t  heap[RSVM_HEAP_BYTES];
    uint16_t heap_used;

    // String pool (literals + string vars)
    char     str_pool[RSVM_STR_POOL];
    uint16_t str_pool_used;

    // Compile-time reference table (static analysis)
    rsvm_ref_ent_t refs[RSVM_MAX_REFS];
    uint8_t        ref_count;

    // Per-function monitors (@monitor_time / _highres / _ram / _execs)
    uint32_t func_execs[RSVM_MAX_FUNCS];
    uint32_t func_last_ms[RSVM_MAX_FUNCS];
    uint32_t func_last_us[RSVM_MAX_FUNCS];
    uint32_t func_last_ram[RSVM_MAX_FUNCS];
    uint64_t func_enter_ns[RSVM_MAX_FUNCS];
    uint32_t func_enter_ram[RSVM_MAX_FUNCS];

    // Compile-time config (set_* directives)
    char     output_bvul[128];   // set_output_bytecode "path"
    bool     want_bvul;
    char     include_base[192];  // directory of root source for includes

    // Property modules (registered at init)
    const rsvm_mdl_vtbl_t* mdl_immut;
    const rsvm_mdl_vtbl_t* mdl_thread;
    const rsvm_mdl_vtbl_t* mdl_property;
    const rsvm_mdl_vtbl_t* mdl_flowmap;

    // Thread module config (```key=val```)
    bool     thread_ifsingle;      // force sequential
    bool     thread_sequencing;    // honor `n`thread order
    uint8_t  thread_desired;       // from desired_threads

    rsvm_host_t   host;
    rsvm_status_t last_status;
    bool          running;
};

void          rsvm_init(rsvm_t* vm);
void          rsvm_set_host(rsvm_t* vm, const rsvm_host_t* host);

rsvm_status_t rsvm_load(rsvm_t* vm, const uint8_t* image, size_t len);
rsvm_status_t rsvm_load_code(rsvm_t* vm, const uint8_t* code, uint16_t len);

uint8_t       rsvm_slot_by_name(rsvm_t* vm, const char* name, bool create);
int           rsvm_func_by_name(const rsvm_t* vm, const char* name);
int           rsvm_struct_by_name(const rsvm_t* vm, const char* name);
int           rsvm_enum_by_name(const rsvm_t* vm, const char* name);

rsvm_status_t rsvm_run(rsvm_t* vm);
rsvm_status_t rsvm_run_unchecked(rsvm_t* vm);
rsvm_status_t rsvm_step(rsvm_t* vm);

size_t        rsvm_pack_image(const rsvm_t* vm, uint8_t* out, size_t out_cap);
uint16_t      rsvm_disasm(const rsvm_t* vm, uint16_t pc, char* buf, size_t buf_len);
const char*   rsvm_status_str(rsvm_status_t s);

// Heap helpers
int16_t       rsvm_heap_alloc(rsvm_t* vm, uint8_t struct_id);
void          rsvm_heap_retain(rsvm_t* vm, int32_t off);
void          rsvm_heap_release(rsvm_t* vm, int32_t off);

// Module registration (called from desktop / ESP init)
void          rsvm_register_modules(rsvm_t* vm);

// Ref-table helper (parser static analysis)
void          rsvm_ref_note(rsvm_t* vm, const char* name, uint8_t kind, uint16_t line);

// .bvul save / load
size_t        rsvm_save_bvul(const rsvm_t* vm, uint8_t* out, size_t out_cap);
rsvm_status_t rsvm_load_bvul(rsvm_t* vm, const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif
