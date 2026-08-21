#pragma once
// =============================================================================
// rs_vm_opcodes – Vulcan light bytecode (v2.1)
// =============================================================================
// v2.1 adds:
//   - Function property bitflags (@immut @threaded @property @shared @mut …)
//   - Struct/var ~ flags (~packed ~aligned ~volatile ~immut)
//   - Output-ignore sugar (~ignore_*)
//   - FlowMap route table (simple rename / data-flow)
//   - Module hooks (vm_modules/vm_mdl_*)
// =============================================================================

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RSVM_MAGIC0   'R'
#define RSVM_MAGIC1   'S'
#define RSVM_MAGIC2   'V'
#define RSVM_MAGIC3   '2'   // v2 image (compatible)
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

// ---------------------------------------------------------------------------
// Value type tags
// ---------------------------------------------------------------------------
typedef enum {
    RSVM_TY_I32   = 0,
    RSVM_TY_BOOL  = 1,
    RSVM_TY_PTR   = 2,   // v = heap offset; aux = struct type id
    RSVM_TY_ENUM  = 3,   // v = ordinal; aux = enum type id
    RSVM_TY_NIL   = 4,
    RSVM_TY_STR   = 5,   // v = offset into str_pool; aux = length (≤255)
    RSVM_TY_ARR   = 6,   // v = heap offset; aux = total elements (≤255 shown)
    RSVM_TY_U8    = 8,
    RSVM_TY_U16   = 9,
    RSVM_TY_U32   = 10,
    RSVM_TY_I8    = 11,
    RSVM_TY_I16   = 12,
    RSVM_TY_F16   = 13,  // IEEE-754 binary16 bits in low 16 of v
    RSVM_TY_F32   = 14,  // IEEE-754 binary32 bit pattern in v
} rsvm_ty_t;

#pragma pack(push, 1)
typedef struct {
    int32_t  v;     // i32 / u32 bits / f32 bits / ptr / str off
    uint8_t  ty;    // rsvm_ty_t
    uint8_t  aux;   // struct/enum id, str len, arr total len (clamp 255)
} rsvm_val_t;
#pragma pack(pop)

// Array heap payload after rsvm_obj_hdr_t:
//   uint8_t ndim; uint8_t dims[4]; then rsvm_val_t elems[product]
#define RSVM_ARR_MAX_DIM 4

// ---------------------------------------------------------------------------
// Function / variable property bitflags (Vulcan @ syntax)
// ---------------------------------------------------------------------------
typedef enum {
    RSVM_PROP_NONE         = 0,
    RSVM_PROP_IMMUT        = 1 << 0,  // @immut
    RSVM_PROP_THREADED     = 1 << 1,  // @threaded
    RSVM_PROP_PROPERTY     = 1 << 2,  // @property
    RSVM_PROP_SHARED       = 1 << 3,  // @shared
    RSVM_PROP_MUT          = 1 << 4,  // @mut
    RSVM_PROP_IGNORE       = 1 << 5,  // ~ignore
    RSVM_PROP_MON_TIME     = 1 << 6,  // @monitor_time (ms)
    RSVM_PROP_MON_RAM      = 1 << 7,  // @monitor_ram
    RSVM_PROP_MON_EXECS    = 1 << 8,  // @monitor_execs
    RSVM_PROP_MON_TIME_HI  = 1 << 9,  // @monitor_time_highres (µs)
    RSVM_PROP_LOOPS_TRAP   = 1 << 10, // @loops_trapdoor — native for over body
    RSVM_PROP_AUTOUNROLL   = 1 << 11, // @autounroll(n) — const-count unroll
    RSVM_PROP_UNLIMITED    = 1 << 12, // @unlimited — no step limit in this fn
    RSVM_PROP_UNSPEC_OUT  = 1 << 13, // @unspecifiedout — out type is dynamic
} rsvm_prop_t;

#ifndef RSVM_MAX_REFS
#define RSVM_MAX_REFS       128
#endif

// Compile-time reference kinds
typedef enum {
    RSVM_REF_VAR   = 0,
    RSVM_REF_FUNC  = 1,
    RSVM_REF_FIELD = 2,
} rsvm_ref_kind_t;

#pragma pack(push, 1)
typedef struct {
    char     name[RSVM_NAME_LEN];
    uint8_t  kind;     // rsvm_ref_kind_t
    uint16_t line;
    uint16_t uses;     // how many times referenced
} rsvm_ref_ent_t;
#pragma pack(pop)

// Struct / field ~ flags
typedef enum {
    RSVM_FLAG_NONE     = 0,
    RSVM_FLAG_PACKED   = 1 << 0,  // ~packed
    RSVM_FLAG_ALIGNED  = 1 << 1,  // ~aligned(n)  (n stored separately)
    RSVM_FLAG_VOLATILE = 1 << 2,  // ~volatile
    RSVM_FLAG_IMMUT    = 1 << 3,  // ~immut
    RSVM_FLAG_COMPOSE  = 1 << 4,  // @compose field (owned sub-object)
} rsvm_flag_t;

// ---------------------------------------------------------------------------
// Opcodes
// ---------------------------------------------------------------------------
typedef enum {
    RSVM_OP_HALT      = 0x00,
    RSVM_OP_NOP       = 0x01,
    RSVM_OP_PRINT_S   = 0x02,  // u8 slot
    RSVM_OP_PRINT_I   = 0x03,  // i32 imm
    RSVM_OP_PRINT_STR = 0x04,  // u8 len, bytes…
    RSVM_OP_PRINT_V   = 0x05,  // pop & print top (typed)

    RSVM_OP_LDI       = 0x10,  // u8 slot, i32 imm  (ty=i32)
    RSVM_OP_MOV       = 0x11,  // u8 dst, u8 src
    RSVM_OP_LOAD      = 0x12,  // u8 slot → push
    RSVM_OP_STORE     = 0x13,  // u8 slot ← pop
    RSVM_OP_LOAD_TY   = 0x14,  // u8 slot, u8 ty, u8 aux, i32 imm

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

    RSVM_OP_JMP       = 0x40,  // i16 rel
    RSVM_OP_JZ        = 0x41,  // i16 rel
    RSVM_OP_JNZ       = 0x42,  // i16 rel

    RSVM_OP_DELAY_MS  = 0x50,
    RSVM_OP_PIN_MODE  = 0x51,
    RSVM_OP_DIG_WR    = 0x52,
    RSVM_OP_DIG_RD    = 0x53,
    RSVM_OP_ADC_RD    = 0x54,
    RSVM_OP_MILLIS    = 0x55,  // push host millis()
    RSVM_OP_GPIO_RD   = 0x56,  // pop pin → push 0/1
    RSVM_OP_GPIO_WR   = 0x57,  // pop lvl, pop pin
    RSVM_OP_ADC_RDP   = 0x58,  // pop pin → push adc

    RSVM_OP_PUSHI     = 0x60,  // i32 (ty i32)
    RSVM_OP_POP       = 0x61,
    RSVM_OP_DUP       = 0x62,
    RSVM_OP_PUSH_NIL  = 0x63,
    RSVM_OP_PUSH_TY   = 0x64,  // u8 ty, u8 aux, i32 v

    // Functions
    RSVM_OP_CALL      = 0x70,  // u8 func_id
    RSVM_OP_RET       = 0x71,  // u8 n_outs (values already on stack)
    RSVM_OP_RET0      = 0x72,  // return no outs

    // Heap / objects / pointers
    RSVM_OP_NEW       = 0x80,  // u8 struct_id → push smart ptr
    RSVM_OP_FIELD_LD  = 0x81,  // u8 field_idx  (obj ptr on stack → field val)
    RSVM_OP_FIELD_ST  = 0x82,  // u8 field_idx  (val, obj ptr → store)
    RSVM_OP_RETAIN    = 0x83,  // top ptr retain
    RSVM_OP_RELEASE   = 0x84,  // top ptr release (pop)
    RSVM_OP_PTR_LD    = 0x85,  // raw: pop ptr-as-i32 slot index → push that slot
    RSVM_OP_PTR_ST    = 0x86,  // raw: pop val, pop slot-index → store

    // Property / flow (v2.1)
    RSVM_OP_PROP_CHK  = 0x90,  // u8 prop_mask  – runtime assert / module hook
    RSVM_OP_FLOW      = 0x91,  // u8 route_id   – apply FlowMap route
    RSVM_OP_IGNORE    = 0x92,  // pop & discard (output ignore)

    // Strings (v2.1+)
    RSVM_OP_STR_LIT   = 0xA0,  // u8 len, bytes… → push TY_STR (interned in pool)
    RSVM_OP_STR_CHAR  = 0xA1,  // pop idx, pop str → push i32 char (0 if OOB)
    RSVM_OP_STR_LEN   = 0xA2,  // pop str → push i32 length
    RSVM_OP_PRINT_C   = 0xA3,  // pop i32 → print single char (no newline)
    RSVM_OP_PRINT_NL  = 0xA4,  // print newline

    // Loop trapdoor: native C for-loop over a fixed body range
    // Encoding: OP_TRAP_LOOP, u16 body_len  ; count on stack; body follows
    RSVM_OP_TRAP_LOOP = 0xB0,

    // Bitwise
    RSVM_OP_BAND      = 0xB1,
    RSVM_OP_BOR       = 0xB2,
    RSVM_OP_BXOR      = 0xB3,
    RSVM_OP_BNOT      = 0xB4,
    RSVM_OP_SHL       = 0xB5,
    RSVM_OP_SHR       = 0xB6,

    // Loop interrupt
    RSVM_OP_BREAK     = 0xB7,  // exit nearest TRAP_LOOP / set break flag
    RSVM_OP_CONTINUE  = 0xB8,

    // File (host)
    RSVM_OP_FREAD     = 0xC0,  // pop path-str → push str (file contents) or nil
    RSVM_OP_FWRITE    = 0xC1,  // pop data-str, pop path-str → push i32 bytes written

    // Arrays (1D i32 heap blob)
    RSVM_OP_ARR_NEW   = 0xC2,  // pop len → 1D array
    RSVM_OP_ARR_LD    = 0xC3,  // pop idx, pop arr → push elem
    RSVM_OP_ARR_ST    = 0xC4,  // pop val, pop idx, pop arr
    RSVM_OP_ARR_NEWD  = 0xC5,  // u8 ndim; pop d0..d{n-1} → multi-dim
    RSVM_OP_ARR_LDN   = 0xC6,  // u8 nidx; pop idx.., pop arr → elem (row-major)
    RSVM_OP_ARR_STN   = 0xC7,  // u8 nidx; pop val, pop idx.., pop arr
    RSVM_OP_FADD      = 0xD0,  // f32 add
    RSVM_OP_FSUB      = 0xD1,
    RSVM_OP_FMUL      = 0xD2,
    RSVM_OP_FDIV      = 0xD3,
    RSVM_OP_CONV      = 0xD4,  // u8 to_ty; convert top of stack

    // Shell / OS integration (bash-like + MWenv / UART / pools)
    // Strings on stack are TY_STR (pool). Host callbacks do real work.
    RSVM_OP_SYS_CMD    = 0xE0,  // pop cmd-str → push result-str (or nil)
    RSVM_OP_SYS_EXEC   = 0xE1,  // pop path-str → run .vul/.bvul; push status i32
    RSVM_OP_SYS_OPEN   = 0xE2,  // pop app-name-str → open registered app; push status
    RSVM_OP_SYS_LS    = 0xE3,  // pop path-str (empty = ".") → push listing-str
    RSVM_OP_SYS_CD     = 0xE4,  // pop path-str → push status i32
    RSVM_OP_SYS_PWD    = 0xE5,  // push cwd-str
    RSVM_OP_MW_TEXT    = 0xE6,  // pop text-str, pop win_id → push status
    RSVM_OP_UART_SEND  = 0xE7,  // pop data-str, pop type-i32 → push nbytes
    RSVM_OP_POOL_OP    = 0xE8,  // pop data-str, pop op-i32, pop name-str → push result-str
    RSVM_OP_SYS_ENV    = 0xE9,  // pop key-str → push value-str (env)
    RSVM_OP_SYS_GREP   = 0xEA,  // pop pattern, pop haystack → push matches-str
    RSVM_OP_SYS_CAT    = 0xEB,  // pop path → push file contents (alias fread)
    RSVM_OP_SYS_ECHO   = 0xEC,  // pop str → print + push same
    RSVM_OP_SYS_HEAD   = 0xED,  // pop n-lines, pop text → push first n lines
    RSVM_OP_SYS_WC     = 0xEE,  // pop text → push i32 line count
    RSVM_OP_SYSCONF_GET= 0xEF,  // pop key-str → push value (i32/str via host)
    RSVM_OP_SYSCONF_SET= 0xF0,  // pop value, pop key-str → push status i32
    RSVM_OP_NATIVE     = 0xF1,  // u8 nargs; pop args…, pop name-str → push i32 result

    RSVM_OP_END       = 0xFF,
} rsvm_op_t;

// Heap object header (in arena)
#pragma pack(push, 1)
typedef struct {
    uint16_t refcount;
    uint8_t  type_id;     // struct id
    uint8_t  nfields;
    // followed by nfields * rsvm_val_t
} rsvm_obj_hdr_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint8_t  magic[4];
    uint8_t  version;
    uint8_t  flags;        // bit0 names, bit1 funcs, bit2 structs
    uint16_t code_len;
    uint16_t entry;        // main entry PC
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
    uint16_t props;        // rsvm_prop_t bitfield  (@immut @threaded …)
} rsvm_func_ent_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    char    name[RSVM_NAME_LEN];
    uint8_t nfields;
    uint8_t flags;         // rsvm_flag_t  (~packed ~volatile …)
    uint8_t align;         // from ~aligned(n), 0 = default
    uint8_t base_id;       // parent struct id, 0xFF = none (inheritance)
    char    field_names[RSVM_MAX_FIELDS][RSVM_NAME_LEN];
    uint8_t field_ty[RSVM_MAX_FIELDS];
    uint8_t field_flags[RSVM_MAX_FIELDS];
    uint8_t field_aux[RSVM_MAX_FIELDS];  // struct id for PTR/@compose fields
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

// Simple FlowMap route: src_name → dst_name (compile-time rename / data-flow)
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
