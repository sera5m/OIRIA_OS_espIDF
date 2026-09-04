#pragma once
// rs_vm_opcodes – Vulcan light bytecode (v2.1) + LUT trig
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RSVM_MAGIC0   'R'
#define RSVM_MAGIC1   'S'
#define RSVM_MAGIC2   'V'
#define RSVM_MAGIC3   '2'
#define RSVM_VERSION  2
#ifndef RSVM_MAX_CODE
#define RSVM_MAX_CODE     4096
#endif
#ifndef RSVM_MAX_SLOTS
#define RSVM_MAX_SLOTS    64
#endif
#ifndef RSVM_MAX_STACK
#define RSVM_MAX_STACK    64
#endif
#ifndef RSVM_MAX_CALLS
#define RSVM_MAX_CALLS    16
#endif
#ifndef RSVM_MAX_FUNCS
#define RSVM_MAX_FUNCS    24
#endif
#ifndef RSVM_MAX_STRUCTS
#define RSVM_MAX_STRUCTS  12
#endif
#ifndef RSVM_MAX_FIELDS
#define RSVM_MAX_FIELDS   12
#endif
#ifndef RSVM_MAX_ENUMS
#define RSVM_MAX_ENUMS    12
#endif
#ifndef RSVM_MAX_ENUM_MEMBERS
#define RSVM_MAX_ENUM_MEMBERS 16
#endif
#ifndef RSVM_HEAP_BYTES
#define RSVM_HEAP_BYTES   2048
#endif
#ifndef RSVM_NAME_LEN
#define RSVM_NAME_LEN     32
#endif
#ifndef RSVM_MAX_STEPS
#define RSVM_MAX_STEPS    200000
#endif
#ifndef RSVM_MAX_FLOW_ROUTES
#define RSVM_MAX_FLOW_ROUTES  32
#endif
#ifndef RSVM_STR_POOL
#define RSVM_STR_POOL         1024
#endif
typedef enum {
    RSVM_TY_I32   = 0,
    RSVM_TY_BOOL  = 1,
    RSVM_TY_PTR   = 2,
    RSVM_TY_ENUM  = 3,
    RSVM_TY_NIL   = 4,
    RSVM_TY_STR   = 5,
    RSVM_TY_ARR   = 6,
    RSVM_TY_U8    = 8,
    RSVM_TY_U16   = 9,
    RSVM_TY_U32   = 10,
    RSVM_TY_I8    = 11,
    RSVM_TY_I16   = 12,
    RSVM_TY_F16   = 13,
    RSVM_TY_F32   = 14,
} rsvm_ty_t;
#pragma pack(push, 1)
typedef struct {
    int32_t  v;
    uint8_t  ty;
    uint8_t  aux;
} rsvm_val_t;
#pragma pack(pop)
#define RSVM_ARR_MAX_DIM 4
typedef enum {
    RSVM_PROP_NONE         = 0,
    RSVM_PROP_IMMUT        = 1 << 0,
    RSVM_PROP_THREADED     = 1 << 1,
    RSVM_PROP_PROPERTY     = 1 << 2,
    RSVM_PROP_SHARED       = 1 << 3,
    RSVM_PROP_MUT          = 1 << 4,
    RSVM_PROP_IGNORE       = 1 << 5,
    RSVM_PROP_MON_TIME     = 1 << 6,
    RSVM_PROP_MON_RAM      = 1 << 7,
    RSVM_PROP_MON_EXECS    = 1 << 8,
    RSVM_PROP_MON_TIME_HI  = 1 << 9,
    RSVM_PROP_LOOPS_TRAP   = 1 << 10,
    RSVM_PROP_AUTOUNROLL   = 1 << 11,
    RSVM_PROP_UNLIMITED    = 1 << 12,
    RSVM_PROP_UNSPEC_OUT  = 1 << 13,
} rsvm_prop_t;
#ifndef RSVM_MAX_REFS
#define RSVM_MAX_REFS       128
#endif
typedef enum {
    RSVM_REF_VAR   = 0,
    RSVM_REF_FUNC  = 1,
    RSVM_REF_FIELD = 2,
} rsvm_ref_kind_t;
#pragma pack(push, 1)
typedef struct {
    char     name[RSVM_NAME_LEN];
    uint8_t  kind;
    uint16_t line;
    uint16_t uses;
} rsvm_ref_ent_t;
#pragma pack(pop)
typedef enum {
    RSVM_FLAG_NONE     = 0,
    RSVM_FLAG_PACKED   = 1 << 0,
    RSVM_FLAG_ALIGNED  = 1 << 1,
    RSVM_FLAG_VOLATILE = 1 << 2,
    RSVM_FLAG_IMMUT    = 1 << 3,
    RSVM_FLAG_COMPOSE  = 1 << 4,
} rsvm_flag_t;
typedef enum {
    RSVM_OP_HALT      = 0x00,
    RSVM_OP_NOP       = 0x01,
    RSVM_OP_PRINT_S   = 0x02,
    RSVM_OP_PRINT_I   = 0x03,
    RSVM_OP_PRINT_STR = 0x04,
    RSVM_OP_PRINT_V   = 0x05,
    RSVM_OP_LDI       = 0x10,
    RSVM_OP_MOV       = 0x11,
    RSVM_OP_LOAD      = 0x12,
    RSVM_OP_STORE     = 0x13,
    RSVM_OP_LOAD_TY   = 0x14,
    RSVM_OP_ADD       = 0x20,
    RSVM_OP_SUB       = 0x21,
    RSVM_OP_MUL       = 0x22,
    RSVM_OP_DIV       = 0x23,
    RSVM_OP_MOD       = 0x24,
    RSVM_OP_NEG       = 0x25,
    RSVM_OP_NOT       = 0x26,
    RSVM_OP_EQ        = 0x30,
    RSVM_OP_NE        = 0x31,
    RSVM_OP_LT        = 0x32,
    RSVM_OP_LE        = 0x33,
    RSVM_OP_GT        = 0x34,
    RSVM_OP_GE        = 0x35,
    RSVM_OP_JMP       = 0x40,
    RSVM_OP_JZ        = 0x41,
    RSVM_OP_JNZ       = 0x42,
    RSVM_OP_DELAY_MS  = 0x50,
    RSVM_OP_PIN_MODE  = 0x51,
    RSVM_OP_DIG_WR    = 0x52,
    RSVM_OP_DIG_RD    = 0x53,
    RSVM_OP_ADC_RD    = 0x54,
    RSVM_OP_MILLIS    = 0x55,
    RSVM_OP_GPIO_RD   = 0x56,
    RSVM_OP_GPIO_WR   = 0x57,
    RSVM_OP_ADC_RDP   = 0x58,
    RSVM_OP_PUSHI     = 0x60,
    RSVM_OP_POP       = 0x61,
    RSVM_OP_DUP       = 0x62,
    RSVM_OP_PUSH_NIL  = 0x63,
    RSVM_OP_PUSH_TY   = 0x64,
    RSVM_OP_CALL      = 0x70,
    RSVM_OP_RET       = 0x71,
    RSVM_OP_RET0      = 0x72,
    RSVM_OP_NEW       = 0x80,
    RSVM_OP_FIELD_LD  = 0x81,
    RSVM_OP_FIELD_ST  = 0x82,
    RSVM_OP_RETAIN    = 0x83,
    RSVM_OP_RELEASE   = 0x84,
    RSVM_OP_PTR_LD    = 0x85,
    RSVM_OP_PTR_ST    = 0x86,
    RSVM_OP_PROP_CHK  = 0x90,
    RSVM_OP_FLOW      = 0x91,
    RSVM_OP_IGNORE    = 0x92,
    RSVM_OP_STR_LIT   = 0xA0,
    RSVM_OP_STR_CHAR  = 0xA1,
    RSVM_OP_STR_LEN   = 0xA2,
    RSVM_OP_PRINT_C   = 0xA3,
    RSVM_OP_PRINT_NL  = 0xA4,
    RSVM_OP_TRAP_LOOP = 0xB0,
    RSVM_OP_BAND      = 0xB1,
    RSVM_OP_BOR       = 0xB2,
    RSVM_OP_BXOR      = 0xB3,
    RSVM_OP_BNOT      = 0xB4,
    RSVM_OP_SHL       = 0xB5,
    RSVM_OP_SHR       = 0xB6,
    RSVM_OP_BREAK     = 0xB7,
    RSVM_OP_CONTINUE  = 0xB8,
    RSVM_OP_FREAD     = 0xC0,
    RSVM_OP_FWRITE    = 0xC1,
    RSVM_OP_ARR_NEW   = 0xC2,
    RSVM_OP_ARR_LD    = 0xC3,
    RSVM_OP_ARR_ST    = 0xC4,
    RSVM_OP_ARR_NEWD  = 0xC5,
    RSVM_OP_ARR_LDN   = 0xC6,
    RSVM_OP_ARR_STN   = 0xC7,
    RSVM_OP_FADD      = 0xD0,
    RSVM_OP_FSUB      = 0xD1,
    RSVM_OP_FMUL      = 0xD2,
    RSVM_OP_FDIV      = 0xD3,
    RSVM_OP_CONV      = 0xD4,
    RSVM_OP_SIN       = 0xD5,
    RSVM_OP_COS       = 0xD6,
    RSVM_OP_TAN       = 0xD7,
    RSVM_OP_SIN_AMP   = 0xD8,
    RSVM_OP_SYS_CMD    = 0xE0,
    RSVM_OP_SYS_EXEC   = 0xE1,
    RSVM_OP_SYS_OPEN   = 0xE2,
    RSVM_OP_SYS_LS    = 0xE3,
    RSVM_OP_SYS_CD     = 0xE4,
    RSVM_OP_SYS_PWD    = 0xE5,
    RSVM_OP_MW_TEXT    = 0xE6,
    RSVM_OP_UART_SEND  = 0xE7,
    RSVM_OP_POOL_OP    = 0xE8,
    RSVM_OP_SYS_ENV    = 0xE9,
    RSVM_OP_SYS_GREP   = 0xEA,
    RSVM_OP_SYS_CAT    = 0xEB,
    RSVM_OP_SYS_ECHO   = 0xEC,
    RSVM_OP_SYS_HEAD   = 0xED,
    RSVM_OP_SYS_WC     = 0xEE,
    RSVM_OP_SYSCONF_GET= 0xEF,
    RSVM_OP_SYSCONF_SET= 0xF0,
    RSVM_OP_NATIVE     = 0xF1,
    RSVM_OP_END       = 0xFF,
} rsvm_op_t;
#pragma pack(push, 1)
typedef struct {
    uint16_t refcount;
    uint8_t  type_id;
    uint8_t  nfields;
} rsvm_obj_hdr_t;
#pragma pack(pop)
#pragma pack(push, 1)
typedef struct {
    uint8_t  magic[4];
    uint8_t  version;
    uint8_t  flags;
    uint16_t code_len;
    uint16_t entry;
    uint16_t step_limit;
    uint8_t  slot_count;
    uint8_t  func_count;
    uint8_t  struct_count;
    uint8_t  enum_count;
} rsvm_image_hdr_t;
#pragma pack(pop)
#pragma pack(push, 1)
typedef struct {
    uint8_t slot;
    char    name[RSVM_NAME_LEN];
} rsvm_name_ent_t;
#pragma pack(pop)
#pragma pack(push, 1)
typedef struct {
    char     name[RSVM_NAME_LEN];
    uint16_t entry_pc;
    uint8_t  n_in;
    uint8_t  n_out;
    uint8_t  n_locals;
    uint8_t  reserved;
    uint8_t  arg_slots[8];
    uint8_t  out_slots[8];
    uint16_t props;
} rsvm_func_ent_t;
#pragma pack(pop)
#pragma pack(push, 1)
typedef struct {
    char    name[RSVM_NAME_LEN];
    uint8_t nfields;
    uint8_t flags;
    uint8_t align;
    uint8_t base_id;
    char    field_names[RSVM_MAX_FIELDS][RSVM_NAME_LEN];
    uint8_t field_ty[RSVM_MAX_FIELDS];
    uint8_t field_flags[RSVM_MAX_FIELDS];
    uint8_t field_aux[RSVM_MAX_FIELDS];
} rsvm_struct_ent_t;
#pragma pack(pop)
#pragma pack(push, 1)
typedef struct {
    char    name[RSVM_NAME_LEN];
    uint8_t nmembers;
    char    member_names[RSVM_MAX_ENUM_MEMBERS][RSVM_NAME_LEN];
    int32_t member_vals[RSVM_MAX_ENUM_MEMBERS];
} rsvm_enum_ent_t;
#pragma pack(pop)
#pragma pack(push, 1)
typedef struct {
    char src[RSVM_NAME_LEN];
    char dst[RSVM_NAME_LEN];
} rsvm_flow_route_t;
#pragma pack(pop)
#ifndef RSDOM_TYPE_VM
#define RSDOM_TYPE_VM   0x10
#endif
#ifdef __cplusplus
}
#endif
