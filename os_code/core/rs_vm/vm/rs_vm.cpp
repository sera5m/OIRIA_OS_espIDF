#include "os_code/core/rs_vm/vm/rs_vm.hpp"
#include "os_code/core/rs_vm/vm_modules/vm_mdl_immut/vm_mdl_immut.h"
#include "os_code/core/rs_vm/vm_modules/vm_mdl_thread/vm_mdl_thread.h"
#include "os_code/core/rs_vm/vm_modules/vm_mdl_property/vm_mdl_property.h"
#include "os_code/core/rs_vm/vm_modules/vm_mdl_flowmap/vm_mdl_flowmap.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static rsvm_val_t V_i32(int32_t x) {
    rsvm_val_t v; v.v = x; v.ty = RSVM_TY_I32; v.aux = 0; return v;
}
static rsvm_val_t V_nil(void) {
    rsvm_val_t v; v.v = 0; v.ty = RSVM_TY_NIL; v.aux = 0; return v;
}

static void push(rsvm_t* vm, rsvm_val_t v) {
    if (vm->sp >= RSVM_MAX_STACK) { vm->last_status = RSVM_ERR_STACK; return; }
    vm->stack[vm->sp++] = v;
}
static rsvm_val_t pop(rsvm_t* vm) {
    if (vm->sp <= 0) { vm->last_status = RSVM_ERR_STACK; return V_nil(); }
    return vm->stack[--vm->sp];
}

static uint8_t rd_u8(rsvm_t* vm) {
    if (vm->pc >= vm->code_len) { vm->last_status = RSVM_ERR_CODE; return 0; }
    return vm->code[vm->pc++];
}
static int16_t rd_i16(rsvm_t* vm) {
    uint8_t lo = rd_u8(vm), hi = rd_u8(vm);
    return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
}
static int32_t rd_i32(rsvm_t* vm) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= ((uint32_t)rd_u8(vm)) << (8 * i);
    return (int32_t)v;
}

void rsvm_init(rsvm_t* vm) {
    if (!vm) return;
    memset(vm, 0, sizeof(*vm));
    vm->step_limit = RSVM_MAX_STEPS;
    vm->ram_limit = RSVM_HEAP_BYTES;
    vm->storage_limit = 0;
    vm->thread_cap = 1;
    vm->last_status = RSVM_OK;
    rsvm_register_modules(vm);
}

void rsvm_set_host(rsvm_t* vm, const rsvm_host_t* host) {
    if (!vm) return;
    if (host) vm->host = *host;
    else memset(&vm->host, 0, sizeof(vm->host));
}

uint8_t rsvm_slot_by_name(rsvm_t* vm, const char* name, bool create) {
    if (!vm || !name || !name[0]) return 0xFF;
    for (uint8_t i = 0; i < vm->slot_count; ++i)
        if (strncmp(vm->names[i], name, RSVM_NAME_LEN) == 0) return i;
    if (!create || vm->slot_count >= RSVM_MAX_SLOTS) return 0xFF;
    uint8_t s = vm->slot_count++;
    strncpy(vm->names[s], name, RSVM_NAME_LEN - 1);
    vm->slots[s] = V_i32(0);
    return s;
}

int rsvm_func_by_name(const rsvm_t* vm, const char* name) {
    if (!vm || !name) return -1;
    for (uint8_t i = 0; i < vm->func_count; ++i)
        if (strncmp(vm->funcs[i].name, name, RSVM_NAME_LEN) == 0) return (int)i;
    return -1;
}
int rsvm_struct_by_name(const rsvm_t* vm, const char* name) {
    if (!vm || !name) return -1;
    for (uint8_t i = 0; i < vm->struct_count; ++i)
        if (strncmp(vm->structs[i].name, name, RSVM_NAME_LEN) == 0) return (int)i;
    return -1;
}
int rsvm_enum_by_name(const rsvm_t* vm, const char* name) {
    if (!vm || !name) return -1;
    for (uint8_t i = 0; i < vm->enum_count; ++i)
        if (strncmp(vm->enums[i].name, name, RSVM_NAME_LEN) == 0) return (int)i;
    return -1;
}

int16_t rsvm_heap_alloc(rsvm_t* vm, uint8_t struct_id) {
    if (!vm || struct_id >= vm->struct_count) return -1;
    uint8_t nf = vm->structs[struct_id].nfields;
    size_t need = sizeof(rsvm_obj_hdr_t) + (size_t)nf * sizeof(rsvm_val_t);
    need = (need + 3u) & ~3u;
    if ((uint16_t)(vm->heap_used + need) > RSVM_HEAP_BYTES) return -1;
    int16_t off = (int16_t)vm->heap_used;
    rsvm_obj_hdr_t* h = (rsvm_obj_hdr_t*)(vm->heap + off);
    h->refcount = 1;
    h->type_id = struct_id;
    h->nfields = nf;
    rsvm_val_t* fields = (rsvm_val_t*)(vm->heap + off + sizeof(rsvm_obj_hdr_t));
    for (uint8_t i = 0; i < nf; ++i) {
        fields[i] = V_i32(0);
        fields[i].ty = vm->structs[struct_id].field_ty[i];
        fields[i].aux = vm->structs[struct_id].field_aux[i];
    }
    vm->heap_used = (uint16_t)(vm->heap_used + need);
    // @compose fields: auto-allocate owned sub-objects
    for (uint8_t i = 0; i < nf; ++i) {
        if (!(vm->structs[struct_id].field_flags[i] & RSVM_FLAG_COMPOSE)) continue;
        uint8_t sid = vm->structs[struct_id].field_aux[i];
        if (sid >= vm->struct_count) continue;
        int16_t child = rsvm_heap_alloc(vm, sid);
        if (child < 0) { vm->last_status = RSVM_ERR_HEAP; return -1; }
        fields[i].ty = RSVM_TY_PTR;
        fields[i].aux = sid;
        fields[i].v = child;
    }
    return off;
}

void rsvm_heap_retain(rsvm_t* vm, int32_t off) {
    if (!vm || off < 0 || (uint16_t)off >= vm->heap_used) return;
    rsvm_obj_hdr_t* h = (rsvm_obj_hdr_t*)(vm->heap + off);
    if (h->refcount < 0xFFFE) h->refcount++;
}

void rsvm_heap_release(rsvm_t* vm, int32_t off) {
    if (!vm || off < 0 || (uint16_t)off >= vm->heap_used) return;
    rsvm_obj_hdr_t* h = (rsvm_obj_hdr_t*)(vm->heap + off);
    if (h->refcount == 0) return;
    if (--h->refcount == 0) h->type_id = 0xFF;
}

static int32_t as_i32(rsvm_val_t v) { return v.v; }
static float as_f32(rsvm_val_t v) {
    if (v.ty == RSVM_TY_F32) { float f; memcpy(&f, &v.v, 4); return f; }
    if (v.ty == RSVM_TY_F16) {
        uint16_t h = (uint16_t)(v.v & 0xFFFF);
        uint32_t sign = (uint32_t)(h >> 15) << 31;
        uint32_t exp = (h >> 10) & 0x1F;
        uint32_t man = h & 0x3FF;
        uint32_t bits;
        if (exp == 0) {
            if (man == 0) bits = sign;
            else {
                exp = 127 - 15 + 1;
                while ((man & 0x400) == 0) { man <<= 1; exp--; }
                man &= 0x3FF;
                bits = sign | (exp << 23) | (man << 13);
            }
        } else if (exp == 31) {
            bits = sign | 0x7F800000u | (man << 13);
        } else {
            bits = sign | ((exp + (127 - 15)) << 23) | (man << 13);
        }
        float f; memcpy(&f, &bits, 4); return f;
    }
    return (float)v.v;
}
static rsvm_val_t V_f32(float f) {
    rsvm_val_t r; r.ty = RSVM_TY_F32; r.aux = 0; memcpy(&r.v, &f, 4); return r;
}
static rsvm_val_t V_f16_from_f32(float f) {
    uint32_t bits; memcpy(&bits, &f, 4);
    uint32_t sign = (bits >> 16) & 0x8000;
    int32_t exp = (int32_t)((bits >> 23) & 0xFF) - 127 + 15;
    uint32_t man = bits & 0x7FFFFF;
    uint16_t h;
    if (exp <= 0) h = (uint16_t)sign;
    else if (exp >= 31) h = (uint16_t)(sign | 0x7C00);
    else h = (uint16_t)(sign | ((uint32_t)exp << 10) | (man >> 13));
    rsvm_val_t r; r.ty = RSVM_TY_F16; r.aux = 0; r.v = (int32_t)h; return r;
}
static int is_float_ty(uint8_t ty) {
    return ty == RSVM_TY_F32 || ty == RSVM_TY_F16;
}

static void print_val(rsvm_t* vm, rsvm_val_t v) {
    if (v.ty == RSVM_TY_PTR) {
        char buf[48];
        snprintf(buf, sizeof buf, "ptr(%ld)#%u", (long)v.v, v.aux);
        if (vm->host.print_str) vm->host.print_str(buf, (uint8_t)strlen(buf), vm->host.user);
        return;
    }
    if (v.ty == RSVM_TY_STR) {
        if (vm->host.print_str && v.v >= 0 &&
            (uint16_t)(v.v + v.aux) <= vm->str_pool_used)
            vm->host.print_str(vm->str_pool + v.v, v.aux, vm->host.user);
        return;
    }
    if (is_float_ty(v.ty)) {
        char buf[48];
        snprintf(buf, sizeof buf, "%g", (double)as_f32(v));
        if (vm->host.print_str) vm->host.print_str(buf, (uint8_t)strlen(buf), vm->host.user);
        else if (vm->host.print_i32) vm->host.print_i32((int32_t)as_f32(v), vm->host.user);
        return;
    }
    if (vm->host.print_i32) vm->host.print_i32(v.v, vm->host.user);
}

// Intern bytes into str_pool; returns offset or -1 on OOM.
static int16_t str_intern(rsvm_t* vm, const char* data, uint8_t len);
static int str_from_val(const rsvm_t* vm, rsvm_val_t v, char* out, int out_max) {
    if (!vm || !out || out_max <= 0) return -1;
    out[0] = 0;
    if (v.ty != RSVM_TY_STR) return -1;
    int n = (int)v.aux;
    if (n < 0) n = 0;
    if (n >= out_max) n = out_max - 1;
    if (v.v < 0 || (uint16_t)(v.v + n) > vm->str_pool_used) return -1;
    memcpy(out, vm->str_pool + v.v, (size_t)n);
    out[n] = 0;
    return n;
}
static void push_cstr(rsvm_t* vm, const char* s, int n) {
    if (!s || n < 0) { push(vm, V_nil()); return; }
    if (n > 255) n = 255;
    int16_t off = str_intern(vm, s, (uint8_t)n);
    if (off < 0) { push(vm, V_nil()); return; }
    rsvm_val_t sv; sv.ty = RSVM_TY_STR; sv.aux = (uint8_t)n; sv.v = off;
    push(vm, sv);
}
static int16_t str_intern(rsvm_t* vm, const char* data, uint8_t len) {
    if (!vm || (uint16_t)(vm->str_pool_used + len) > RSVM_STR_POOL) return -1;
    int16_t off = (int16_t)vm->str_pool_used;
    memcpy(vm->str_pool + off, data, len);
    vm->str_pool_used = (uint16_t)(vm->str_pool_used + len);
    return off;
}

rsvm_status_t rsvm_load(rsvm_t* vm, const uint8_t* image, size_t len) {
    if (!vm || !image || len < sizeof(rsvm_image_hdr_t)) return RSVM_ERR_BAD_MAGIC;
    const rsvm_image_hdr_t* h = (const rsvm_image_hdr_t*)image;
    if (h->magic[0] != RSVM_MAGIC0 || h->magic[1] != RSVM_MAGIC1 ||
        h->magic[2] != RSVM_MAGIC2 || (h->magic[3] != '1' && h->magic[3] != '2'))
        return RSVM_ERR_BAD_MAGIC;
    if (h->code_len > RSVM_MAX_CODE) return RSVM_ERR_CODE;
    rsvm_host_t host = vm->host;
    rsvm_init(vm);
    vm->host = host;
    vm->code_len = h->code_len;
    vm->entry = h->entry;
    vm->pc = h->entry;
    vm->step_limit = h->step_limit ? h->step_limit : RSVM_MAX_STEPS;
    size_t off = sizeof(rsvm_image_hdr_t);
    if (off + h->code_len > len) return RSVM_ERR_CODE;
    memcpy(vm->code, image + off, h->code_len);
    return RSVM_OK;
}

rsvm_status_t rsvm_load_code(rsvm_t* vm, const uint8_t* code, uint16_t len) {
    if (!vm || !code || len > RSVM_MAX_CODE) return RSVM_ERR_CODE;
    rsvm_host_t host = vm->host;
    rsvm_func_ent_t funcs[RSVM_MAX_FUNCS];
    rsvm_struct_ent_t structs[RSVM_MAX_STRUCTS];
    rsvm_enum_ent_t enums[RSVM_MAX_ENUMS];
    char names[RSVM_MAX_SLOTS][RSVM_NAME_LEN];
    uint8_t fc = vm->func_count, sc = vm->struct_count, ec = vm->enum_count, nc = vm->slot_count;
    memcpy(funcs, vm->funcs, sizeof funcs);
    memcpy(structs, vm->structs, sizeof structs);
    memcpy(enums, vm->enums, sizeof enums);
    memcpy(names, vm->names, sizeof names);
    rsvm_init(vm);
    vm->host = host;
    vm->func_count = fc; vm->struct_count = sc; vm->enum_count = ec; vm->slot_count = nc;
    memcpy(vm->funcs, funcs, sizeof funcs);
    memcpy(vm->structs, structs, sizeof structs);
    memcpy(vm->enums, enums, sizeof enums);
    memcpy(vm->names, names, sizeof names);
    memcpy(vm->code, code, len);
    vm->code_len = len;
    return RSVM_OK;
}

size_t rsvm_pack_image(const rsvm_t* vm, uint8_t* out, size_t out_cap) {
    if (!vm || !out) return 0;
    size_t need = sizeof(rsvm_image_hdr_t) + vm->code_len;
    if (need > out_cap) return 0;
    rsvm_image_hdr_t* h = (rsvm_image_hdr_t*)out;
    memset(h, 0, sizeof(*h));
    h->magic[0] = RSVM_MAGIC0; h->magic[1] = RSVM_MAGIC1;
    h->magic[2] = RSVM_MAGIC2; h->magic[3] = RSVM_MAGIC3;
    h->version = RSVM_VERSION;
    h->code_len = vm->code_len;
    h->entry = vm->entry;
    h->step_limit = (uint16_t)(vm->step_limit > 0xFFFF ? 0xFFFF : vm->step_limit);
    h->slot_count = vm->slot_count;
    h->func_count = vm->func_count;
    h->struct_count = vm->struct_count;
    h->enum_count = vm->enum_count;
    memcpy(out + sizeof(*h), vm->code, vm->code_len);
    return need;
}

const char* rsvm_status_str(rsvm_status_t s) {
    switch (s) {
        case RSVM_OK: return "OK";
        case RSVM_ERR_BAD_MAGIC: return "BAD_MAGIC";
        case RSVM_ERR_CODE: return "CODE";
        case RSVM_ERR_STACK: return "STACK";
        case RSVM_ERR_SLOT: return "SLOT";
        case RSVM_ERR_STEP_LIMIT: return "STEP_LIMIT";
        case RSVM_ERR_DIV0: return "DIV0";
        case RSVM_ERR_HOST: return "HOST";
        case RSVM_ERR_CALL: return "CALL";
        case RSVM_ERR_HEAP: return "HEAP";
        case RSVM_ERR_TYPE: return "TYPE";
        case RSVM_ERR_NULL_PTR: return "NULL_PTR";
        case RSVM_ERR_PROP: return "PROP";
        default: return "?";
    }
}




rsvm_status_t rsvm_step(rsvm_t* vm) {
    if (!vm) return RSVM_ERR_CODE;
    if (vm->last_status != RSVM_OK) return vm->last_status;
    if (vm->pc >= vm->code_len) { vm->running = false; return RSVM_OK; }
    if (vm->steps >= vm->step_limit) {
        vm->last_status = RSVM_ERR_STEP_LIMIT;
        vm->running = false;
        return vm->last_status;
    }
    vm->steps++;
    uint8_t op = rd_u8(vm);

    switch (op) {
    case RSVM_OP_HALT:
    case RSVM_OP_END:
        vm->running = false; break;
    case RSVM_OP_NOP: break;

    case RSVM_OP_PUSHI: push(vm, V_i32(rd_i32(vm))); break;
    case RSVM_OP_PUSH_NIL: push(vm, V_nil()); break;
    case RSVM_OP_PUSH_TY: {
        uint8_t ty = rd_u8(vm), aux = rd_u8(vm);
        int32_t v = rd_i32(vm);
        rsvm_val_t x; x.ty = ty; x.aux = aux; x.v = v;
        push(vm, x);
        break;
    }
    case RSVM_OP_POP: (void)pop(vm); break;
    case RSVM_OP_DUP: {
        if (vm->sp <= 0) { vm->last_status = RSVM_ERR_STACK; break; }
        push(vm, vm->stack[vm->sp - 1]);
        break;
    }

    case RSVM_OP_LDI: {
        uint8_t s = rd_u8(vm); int32_t v = rd_i32(vm);
        if (s >= RSVM_MAX_SLOTS) { vm->last_status = RSVM_ERR_SLOT; break; }
        vm->slots[s] = V_i32(v);
        break;
    }
    case RSVM_OP_LOAD_TY: {
        uint8_t s = rd_u8(vm), ty = rd_u8(vm), aux = rd_u8(vm);
        int32_t v = rd_i32(vm);
        if (s >= RSVM_MAX_SLOTS) { vm->last_status = RSVM_ERR_SLOT; break; }
        vm->slots[s].v = v; vm->slots[s].ty = ty; vm->slots[s].aux = aux;
        break;
    }
    case RSVM_OP_MOV: {
        uint8_t d = rd_u8(vm), s = rd_u8(vm);
        if (d >= RSVM_MAX_SLOTS || s >= RSVM_MAX_SLOTS) {
            vm->last_status = RSVM_ERR_SLOT; break;
        }
        if (vm->slots[d].ty == RSVM_TY_PTR) rsvm_heap_release(vm, vm->slots[d].v);
        vm->slots[d] = vm->slots[s];
        if (vm->slots[d].ty == RSVM_TY_PTR) rsvm_heap_retain(vm, vm->slots[d].v);
        break;
    }
    case RSVM_OP_LOAD: {
        uint8_t s = rd_u8(vm);
        if (s >= RSVM_MAX_SLOTS) { vm->last_status = RSVM_ERR_SLOT; break; }
        rsvm_val_t v = vm->slots[s];
        if (v.ty == RSVM_TY_PTR) rsvm_heap_retain(vm, v.v);
        push(vm, v);
        break;
    }
    case RSVM_OP_STORE: {
        uint8_t s = rd_u8(vm);
        if (s >= RSVM_MAX_SLOTS) { vm->last_status = RSVM_ERR_SLOT; break; }
        // property modules may veto / log external writes under @immut
        if (vm->mdl_immut && vm->mdl_immut->on_store) vm->mdl_immut->on_store(vm, s);
        if (vm->mdl_property && vm->mdl_property->on_store) vm->mdl_property->on_store(vm, s);
        rsvm_val_t v = pop(vm);
        if (vm->slots[s].ty == RSVM_TY_PTR) rsvm_heap_release(vm, vm->slots[s].v);
        vm->slots[s] = v;
        break;
    }

    case RSVM_OP_ADD: {
        rsvm_val_t b=pop(vm), a=pop(vm);
        if (is_float_ty(a.ty) || is_float_ty(b.ty)) push(vm, V_f32(as_f32(a)+as_f32(b)));
        else push(vm, V_i32(as_i32(a)+as_i32(b)));
        break;
    }
    case RSVM_OP_SUB: {
        rsvm_val_t b=pop(vm), a=pop(vm);
        if (is_float_ty(a.ty) || is_float_ty(b.ty)) push(vm, V_f32(as_f32(a)-as_f32(b)));
        else push(vm, V_i32(as_i32(a)-as_i32(b)));
        break;
    }
    case RSVM_OP_MUL: {
        rsvm_val_t b=pop(vm), a=pop(vm);
        if (is_float_ty(a.ty) || is_float_ty(b.ty)) push(vm, V_f32(as_f32(a)*as_f32(b)));
        else push(vm, V_i32(as_i32(a)*as_i32(b)));
        break;
    }
    case RSVM_OP_DIV: {
        rsvm_val_t b=pop(vm), a=pop(vm);
        if (is_float_ty(a.ty) || is_float_ty(b.ty)) {
            float fb = as_f32(b);
            push(vm, V_f32(fb != 0.f ? as_f32(a)/fb : 0.f));
        } else {
            push(vm, V_i32(as_i32(b)==0 ? 0 : as_i32(a)/as_i32(b)));
        }
        break;
    }
    case RSVM_OP_MOD: {
        rsvm_val_t b=pop(vm), a=pop(vm);
        push(vm, V_i32(as_i32(b)==0 ? 0 : as_i32(a)%as_i32(b)));
        break;
    }
    case RSVM_OP_NEG: {
        rsvm_val_t a = pop(vm);
        if (is_float_ty(a.ty)) push(vm, V_f32(-as_f32(a)));
        else push(vm, V_i32(-as_i32(a)));
        break;
    }
    case RSVM_OP_NOT: push(vm, V_i32(as_i32(pop(vm)) ? 0 : 1)); break;

    case RSVM_OP_EQ:  { rsvm_val_t b=pop(vm), a=pop(vm); push(vm, V_i32(as_i32(a)==as_i32(b))); break; }
    case RSVM_OP_NE:  { rsvm_val_t b=pop(vm), a=pop(vm); push(vm, V_i32(as_i32(a)!=as_i32(b))); break; }
    case RSVM_OP_LT:  { rsvm_val_t b=pop(vm), a=pop(vm); push(vm, V_i32(as_i32(a)< as_i32(b))); break; }
    case RSVM_OP_LE:  { rsvm_val_t b=pop(vm), a=pop(vm); push(vm, V_i32(as_i32(a)<=as_i32(b))); break; }
    case RSVM_OP_GT:  { rsvm_val_t b=pop(vm), a=pop(vm); push(vm, V_i32(as_i32(a)> as_i32(b))); break; }
    case RSVM_OP_GE:  { rsvm_val_t b=pop(vm), a=pop(vm); push(vm, V_i32(as_i32(a)>=as_i32(b))); break; }

    case RSVM_OP_JMP: {
        int16_t rel = rd_i16(vm);
        vm->pc = (uint16_t)((int32_t)vm->pc + rel);
        break;
    }
    case RSVM_OP_JZ: {
        int16_t rel = rd_i16(vm);
        if (as_i32(pop(vm)) == 0) vm->pc = (uint16_t)((int32_t)vm->pc + rel);
        break;
    }
    case RSVM_OP_JNZ: {
        int16_t rel = rd_i16(vm);
        if (as_i32(pop(vm)) != 0) vm->pc = (uint16_t)((int32_t)vm->pc + rel);
        break;
    }

    case RSVM_OP_PRINT_S: {
        uint8_t s = rd_u8(vm);
        if (s >= RSVM_MAX_SLOTS) { vm->last_status = RSVM_ERR_SLOT; break; }
        print_val(vm, vm->slots[s]);
        break;
    }
    case RSVM_OP_PRINT_I: {
        int32_t v = rd_i32(vm);
        if (vm->host.print_i32) vm->host.print_i32(v, vm->host.user);
        break;
    }
    case RSVM_OP_PRINT_STR: {
        uint8_t n = rd_u8(vm);
        if ((uint16_t)(vm->pc + n) > vm->code_len) { vm->last_status = RSVM_ERR_CODE; break; }
        if (vm->host.print_str) vm->host.print_str((const char*)&vm->code[vm->pc], n, vm->host.user);
        vm->pc = (uint16_t)(vm->pc + n);
        break;
    }
    case RSVM_OP_PRINT_V: print_val(vm, pop(vm)); break;

    case RSVM_OP_DELAY_MS: {
        int32_t ms = rd_i32(vm);
        if (vm->host.delay_ms) vm->host.delay_ms((uint32_t)(ms < 0 ? 0 : ms), vm->host.user);
        break;
    }
    case RSVM_OP_PIN_MODE: {
        uint8_t pin = rd_u8(vm), mode = rd_u8(vm);
        if (vm->host.pin_mode) vm->host.pin_mode(pin, mode, vm->host.user);
        break;
    }
    case RSVM_OP_DIG_WR: {
        uint8_t pin = rd_u8(vm), lvl = rd_u8(vm);
        if (vm->host.dig_write) vm->host.dig_write(pin, lvl, vm->host.user);
        break;
    }
    case RSVM_OP_DIG_RD: {
        uint8_t pin = rd_u8(vm), dst = rd_u8(vm);
        if (dst >= RSVM_MAX_SLOTS) { vm->last_status = RSVM_ERR_SLOT; break; }
        int v = vm->host.dig_read ? vm->host.dig_read(pin, vm->host.user) : 0;
        vm->slots[dst] = V_i32(v ? 1 : 0);
        break;
    }
    case RSVM_OP_ADC_RD: {
        uint8_t pin = rd_u8(vm), dst = rd_u8(vm);
        if (dst >= RSVM_MAX_SLOTS) { vm->last_status = RSVM_ERR_SLOT; break; }
        int v = vm->host.adc_read ? vm->host.adc_read(pin, vm->host.user) : 0;
        vm->slots[dst] = V_i32(v);
        break;
    }

    case RSVM_OP_CALL: {
        uint8_t fid = rd_u8(vm);
        if (fid >= vm->func_count) { vm->last_status = RSVM_ERR_CALL; break; }
        if (vm->csp >= RSVM_MAX_CALLS) { vm->last_status = RSVM_ERR_CALL; break; }
        rsvm_func_ent_t* f = &vm->funcs[fid];
        if (vm->sp < f->n_in) { vm->last_status = RSVM_ERR_STACK; break; }
        for (int i = f->n_in - 1; i >= 0; --i) {
            uint8_t s = f->arg_slots[i];
            if (s >= RSVM_MAX_SLOTS) { vm->last_status = RSVM_ERR_SLOT; break; }
            if (vm->slots[s].ty == RSVM_TY_PTR) rsvm_heap_release(vm, vm->slots[s].v);
            vm->slots[s] = pop(vm);
        }
        rsvm_frame_t* fr = &vm->calls[vm->csp++];
        fr->ret_pc = vm->pc;
        fr->fp = vm->frame_base;
        fr->sp_save = vm->sp;
        fr->n_outs = f->n_out;
        fr->func_id = fid;
        fr->step_limit_save = vm->step_limit;
        if (f->props & RSVM_PROP_UNLIMITED) vm->step_limit = 0xFFFFFFFFu;
        // property module enter hooks
        if (f->props & RSVM_PROP_IMMUT && vm->mdl_immut && vm->mdl_immut->on_func_enter)
            vm->mdl_immut->on_func_enter(vm, fid);
        if (f->props & RSVM_PROP_THREADED && vm->mdl_thread && vm->mdl_thread->on_func_enter)
            vm->mdl_thread->on_func_enter(vm, fid);
        if (f->props & RSVM_PROP_PROPERTY && vm->mdl_property && vm->mdl_property->on_func_enter)
            vm->mdl_property->on_func_enter(vm, fid);
        if (f->props & (RSVM_PROP_MON_TIME | RSVM_PROP_MON_TIME_HI | RSVM_PROP_MON_RAM | RSVM_PROP_MON_EXECS)) {
            vm->func_execs[fid]++;
            if (f->props & (RSVM_PROP_MON_TIME | RSVM_PROP_MON_TIME_HI)) {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                vm->func_enter_ns[fid] =
                    (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
            }
            if (f->props & RSVM_PROP_MON_RAM)
                vm->func_enter_ram[fid] =
                    (uint32_t)vm->heap_used + (uint32_t)vm->str_pool_used;
        }
        vm->pc = f->entry_pc;
        break;
    }
    case RSVM_OP_RET:
    case RSVM_OP_RET0: {
        uint8_t n_outs = (op == RSVM_OP_RET0) ? 0 : rd_u8(vm);
        if (vm->csp <= 0) { vm->running = false; break; }
        rsvm_val_t outs[8];
        if (n_outs > 8) n_outs = 8;
        for (int i = n_outs - 1; i >= 0; --i) outs[i] = pop(vm);

        rsvm_frame_t fr = vm->calls[--vm->csp];
        vm->step_limit = fr.step_limit_save;
        // property module exit hooks
        if (fr.func_id < vm->func_count) {
            uint16_t props = vm->funcs[fr.func_id].props;
            if (props & RSVM_PROP_IMMUT && vm->mdl_immut && vm->mdl_immut->on_func_exit)
                vm->mdl_immut->on_func_exit(vm, fr.func_id);
            if (props & RSVM_PROP_THREADED && vm->mdl_thread && vm->mdl_thread->on_func_exit)
                vm->mdl_thread->on_func_exit(vm, fr.func_id);
            if (props & RSVM_PROP_PROPERTY && vm->mdl_property && vm->mdl_property->on_func_exit)
                vm->mdl_property->on_func_exit(vm, fr.func_id);
            if (props & (RSVM_PROP_MON_TIME | RSVM_PROP_MON_TIME_HI | RSVM_PROP_MON_RAM | RSVM_PROP_MON_EXECS)) {
                const char* fname = vm->funcs[fr.func_id].name;
                if (props & (RSVM_PROP_MON_TIME | RSVM_PROP_MON_TIME_HI)) {
                    struct timespec ts;
                    clock_gettime(CLOCK_MONOTONIC, &ts);
                    uint64_t now =
                        (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
                    uint64_t dt = now - vm->func_enter_ns[fr.func_id];
                    if (props & RSVM_PROP_MON_TIME) {
                        uint32_t ms = (uint32_t)(dt / 1000000ull);
                        vm->func_last_ms[fr.func_id] = ms;
                        printf("[monitor_time] %s: %lu ms\n", fname, ms);
                    }
                    if (props & RSVM_PROP_MON_TIME_HI) {
                        uint32_t us = (uint32_t)(dt / 1000ull);
                        vm->func_last_us[fr.func_id] = us;
                        printf("[monitor_time_highres] %s: %lu us\n", fname, us);
                    }
                }
                if (props & RSVM_PROP_MON_RAM) {
                    uint32_t now_ram =
                        (uint32_t)vm->heap_used + (uint32_t)vm->str_pool_used;
                    uint32_t delta = now_ram - vm->func_enter_ram[fr.func_id];
                    vm->func_last_ram[fr.func_id] = delta;
                    printf("[monitor_ram] %s: +%lu bytes (heap+str)\n", fname, delta);
                }
                if (props & RSVM_PROP_MON_EXECS) {
                    printf("[monitor_execs] call %lu of %s\n",
                           vm->func_execs[fr.func_id], fname);
                }
            }
        }
        vm->pc = fr.ret_pc;
        vm->frame_base = fr.fp;
        vm->sp = fr.sp_save;
        for (uint8_t i = 0; i < n_outs; ++i) push(vm, outs[i]);
        break;
    }

    case RSVM_OP_NEW: {
        uint8_t sid = rd_u8(vm);
        int16_t off = rsvm_heap_alloc(vm, sid);
        if (off < 0) { vm->last_status = RSVM_ERR_HEAP; break; }
        rsvm_val_t p; p.ty = RSVM_TY_PTR; p.aux = sid; p.v = off;
        push(vm, p);
        break;
    }
    case RSVM_OP_FIELD_LD: {
        uint8_t fi = rd_u8(vm);
        rsvm_val_t p = pop(vm);
        if (p.ty != RSVM_TY_PTR) { vm->last_status = RSVM_ERR_TYPE; break; }
        if (p.v < 0) { vm->last_status = RSVM_ERR_NULL_PTR; break; }
        rsvm_obj_hdr_t* h = (rsvm_obj_hdr_t*)(vm->heap + p.v);
        if (fi >= h->nfields) { vm->last_status = RSVM_ERR_TYPE; break; }
        rsvm_val_t* fields = (rsvm_val_t*)(vm->heap + p.v + sizeof(rsvm_obj_hdr_t));
        rsvm_val_t fv = fields[fi];
        if (fv.ty == RSVM_TY_PTR) rsvm_heap_retain(vm, fv.v);
        push(vm, fv);
        rsvm_heap_release(vm, p.v);
        break;
    }
    case RSVM_OP_FIELD_ST: {
        uint8_t fi = rd_u8(vm);
        rsvm_val_t p = pop(vm);
        rsvm_val_t val = pop(vm);
        if (p.ty != RSVM_TY_PTR) { vm->last_status = RSVM_ERR_TYPE; break; }
        if (p.v < 0) { vm->last_status = RSVM_ERR_NULL_PTR; break; }
        rsvm_obj_hdr_t* h = (rsvm_obj_hdr_t*)(vm->heap + p.v);
        if (fi >= h->nfields) { vm->last_status = RSVM_ERR_TYPE; break; }
        rsvm_val_t* fields = (rsvm_val_t*)(vm->heap + p.v + sizeof(rsvm_obj_hdr_t));
        if (fields[fi].ty == RSVM_TY_PTR) rsvm_heap_release(vm, fields[fi].v);
        fields[fi] = val;
        rsvm_heap_release(vm, p.v);
        break;
    }
    case RSVM_OP_RETAIN: {
        if (vm->sp <= 0) { vm->last_status = RSVM_ERR_STACK; break; }
        rsvm_val_t* t = &vm->stack[vm->sp - 1];
        if (t->ty == RSVM_TY_PTR) rsvm_heap_retain(vm, t->v);
        break;
    }
    case RSVM_OP_RELEASE: {
        rsvm_val_t p = pop(vm);
        if (p.ty == RSVM_TY_PTR) rsvm_heap_release(vm, p.v);
        break;
    }
    case RSVM_OP_PTR_LD: {
        rsvm_val_t idx = pop(vm);
        uint8_t s = (uint8_t)as_i32(idx);
        if (s >= RSVM_MAX_SLOTS) { vm->last_status = RSVM_ERR_SLOT; break; }
        push(vm, vm->slots[s]);
        break;
    }
    case RSVM_OP_PTR_ST: {
        rsvm_val_t idx = pop(vm);
        rsvm_val_t val = pop(vm);
        uint8_t s = (uint8_t)as_i32(idx);
        if (s >= RSVM_MAX_SLOTS) { vm->last_status = RSVM_ERR_SLOT; break; }
        if (vm->slots[s].ty == RSVM_TY_PTR) rsvm_heap_release(vm, vm->slots[s].v);
        vm->slots[s] = val;
        break;
    }

    case RSVM_OP_PROP_CHK: {
        uint8_t mask = rd_u8(vm);
        (void)mask; // reserved for future hard checks
        break;
    }
    case RSVM_OP_FLOW: {
        uint8_t rid = rd_u8(vm);
        if (vm->mdl_flowmap) {
            // use module helper if linked; otherwise inline
            extern int rsvm_flow_apply(rsvm_t*, uint8_t);
            rsvm_flow_apply(vm, rid);
        } else if (rid < vm->flow_count) {
            const rsvm_flow_route_t* r = &vm->flows[rid];
            uint8_t src = rsvm_slot_by_name(vm, r->src, false);
            uint8_t dst = rsvm_slot_by_name(vm, r->dst, true);
            if (src != 0xFF && dst != 0xFF) vm->slots[dst] = vm->slots[src];
        }
        break;
    }
    case RSVM_OP_TRAP_LOOP: {
        // count on stack; u16 body_len; body bytes follow
        rsvm_val_t cv = pop(vm);
        int32_t n = as_i32(cv);
        uint8_t lo = rd_u8(vm), hi = rd_u8(vm);
        uint16_t blen = (uint16_t)lo | ((uint16_t)hi << 8);
        uint16_t body = vm->pc;
        uint16_t after = (uint16_t)(body + blen);
        if (after > vm->code_len) { vm->last_status = RSVM_ERR_CODE; break; }
        if (n < 0) n = 0;
        // Native C for-loop: re-enter body range each iteration (trapdoor)
        for (int32_t i = 0; i < n && vm->running && vm->last_status == RSVM_OK; ++i) {
            vm->break_flag = 0;
            vm->cont_flag = 0;
            vm->pc = body;
            while (vm->pc < after && vm->running && vm->last_status == RSVM_OK) {
                rsvm_step(vm);
                if (vm->break_flag) { vm->break_flag = 0; goto trap_done; }
                if (vm->cont_flag) { vm->cont_flag = 0; break; } // next iter
            }
        }
        trap_done:
        vm->pc = after;
        break;
    }


    case RSVM_OP_BAND: {
        rsvm_val_t b = pop(vm), a = pop(vm);
        push(vm, V_i32(as_i32(a) & as_i32(b))); break;
    }
    case RSVM_OP_BOR: {
        rsvm_val_t b = pop(vm), a = pop(vm);
        push(vm, V_i32(as_i32(a) | as_i32(b))); break;
    }
    case RSVM_OP_BXOR: {
        rsvm_val_t b = pop(vm), a = pop(vm);
        push(vm, V_i32(as_i32(a) ^ as_i32(b))); break;
    }
    case RSVM_OP_BNOT: {
        rsvm_val_t a = pop(vm);
        push(vm, V_i32(~as_i32(a))); break;
    }
    case RSVM_OP_SHL: {
        rsvm_val_t b = pop(vm), a = pop(vm);
        push(vm, V_i32(as_i32(a) << (as_i32(b) & 31))); break;
    }
    case RSVM_OP_SHR: {
        rsvm_val_t b = pop(vm), a = pop(vm);
        push(vm, V_i32(as_i32(a) >> (as_i32(b) & 31))); break;
    }
    case RSVM_OP_BREAK:
        vm->break_flag = 1; break;
    case RSVM_OP_CONTINUE:
        vm->cont_flag = 1; break;

    case RSVM_OP_FREAD: {
        rsvm_val_t pathv = pop(vm);
        if (pathv.ty != RSVM_TY_STR || !vm->host.file_read) {
            push(vm, V_nil()); break;
        }
        char path[128];
        uint8_t plen = pathv.aux;
        if (plen > 127) plen = 127;
        if (pathv.v < 0 || (uint16_t)pathv.v + plen > vm->str_pool_used) {
            push(vm, V_nil()); break;
        }
        memcpy(path, vm->str_pool + pathv.v, plen); path[plen] = 0;
        char buf[512];
        int n = vm->host.file_read(path, buf, (int)sizeof(buf) - 1, vm->host.user);
        if (n < 0) { push(vm, V_nil()); break; }
        if (n > 255) n = 255;
        // intern
        if (vm->str_pool_used + (uint16_t)n + 1 > RSVM_STR_POOL) {
            push(vm, V_nil()); break;
        }
        uint16_t off = vm->str_pool_used;
        memcpy(vm->str_pool + off, buf, (size_t)n);
        vm->str_pool_used = (uint16_t)(off + n);
        rsvm_val_t sv; sv.ty = RSVM_TY_STR; sv.aux = (uint8_t)n; sv.v = off;
        push(vm, sv);
        break;
    }
    case RSVM_OP_FWRITE: {
        rsvm_val_t data = pop(vm);
        rsvm_val_t pathv = pop(vm);
        if (pathv.ty != RSVM_TY_STR || data.ty != RSVM_TY_STR || !vm->host.file_write) {
            push(vm, V_i32(-1)); break;
        }
        char path[128];
        uint8_t plen = pathv.aux; if (plen > 127) plen = 127;
        memcpy(path, vm->str_pool + pathv.v, plen); path[plen] = 0;
        const char* d = vm->str_pool + data.v;
        int n = vm->host.file_write(path, d, (int)data.aux, vm->host.user);
        push(vm, V_i32(n));
        break;
    }
    case RSVM_OP_ARR_NEW: {
        int32_t len = as_i32(pop(vm));
        if (len < 0) len = 0;
        if (len > 512) len = 512;
        size_t meta = 1 + RSVM_ARR_MAX_DIM; // ndim + dims[4]
        size_t need = sizeof(rsvm_obj_hdr_t) + meta + (size_t)len * sizeof(rsvm_val_t);
        need = (need + 3u) & ~3u;
        if ((uint16_t)(vm->heap_used + need) > RSVM_HEAP_BYTES ||
            (vm->ram_limit && (uint32_t)(vm->heap_used + need) > vm->ram_limit)) {
            vm->last_status = RSVM_ERR_HEAP; break;
        }
        int16_t off = (int16_t)vm->heap_used;
        rsvm_obj_hdr_t* h = (rsvm_obj_hdr_t*)(vm->heap + off);
        h->refcount = 1; h->type_id = 0xFE; h->nfields = (uint8_t)(len > 255 ? 255 : len);
        uint8_t* meta_p = vm->heap + off + sizeof(rsvm_obj_hdr_t);
        meta_p[0] = 1; meta_p[1] = (uint8_t)(len > 255 ? 255 : len);
        for (int d = 2; d <= RSVM_ARR_MAX_DIM; ++d) meta_p[d] = 0;
        rsvm_val_t* el = (rsvm_val_t*)(vm->heap + off + sizeof(rsvm_obj_hdr_t) + meta);
        for (int32_t i = 0; i < len; ++i) el[i] = V_i32(0);
        vm->heap_used = (uint16_t)(vm->heap_used + need);
        rsvm_val_t a; a.ty = RSVM_TY_ARR; a.aux = (uint8_t)(len > 255 ? 255 : len); a.v = off;
        push(vm, a);
        break;
    }
    case RSVM_OP_ARR_NEWD: {
        uint8_t ndim = rd_u8(vm);
        if (ndim < 1) ndim = 1;
        if (ndim > RSVM_ARR_MAX_DIM) ndim = RSVM_ARR_MAX_DIM;
        int32_t dims[RSVM_ARR_MAX_DIM] = {0};
        int32_t total = 1;
        for (int i = (int)ndim - 1; i >= 0; --i) {
            dims[i] = as_i32(pop(vm));
            if (dims[i] < 0) dims[i] = 0;
            if (dims[i] > 256) dims[i] = 256;
            total *= dims[i];
        }
        if (total > 512) total = 512;
        size_t meta = 1 + RSVM_ARR_MAX_DIM;
        size_t need = sizeof(rsvm_obj_hdr_t) + meta + (size_t)total * sizeof(rsvm_val_t);
        need = (need + 3u) & ~3u;
        if ((uint16_t)(vm->heap_used + need) > RSVM_HEAP_BYTES ||
            (vm->ram_limit && (uint32_t)(vm->heap_used + need) > vm->ram_limit)) {
            vm->last_status = RSVM_ERR_HEAP; break;
        }
        int16_t off = (int16_t)vm->heap_used;
        rsvm_obj_hdr_t* h = (rsvm_obj_hdr_t*)(vm->heap + off);
        h->refcount = 1; h->type_id = 0xFE; h->nfields = (uint8_t)(total > 255 ? 255 : total);
        uint8_t* meta_p = vm->heap + off + sizeof(rsvm_obj_hdr_t);
        meta_p[0] = ndim;
        for (int d = 0; d < RSVM_ARR_MAX_DIM; ++d)
            meta_p[1 + d] = (uint8_t)(d < ndim ? dims[d] : 0);
        rsvm_val_t* el = (rsvm_val_t*)(vm->heap + off + sizeof(rsvm_obj_hdr_t) + meta);
        for (int32_t i = 0; i < total; ++i) el[i] = V_i32(0);
        vm->heap_used = (uint16_t)(vm->heap_used + need);
        rsvm_val_t a; a.ty = RSVM_TY_ARR; a.aux = (uint8_t)(total > 255 ? 255 : total); a.v = off;
        push(vm, a);
        break;
    }
    case RSVM_OP_ARR_LD:
    case RSVM_OP_ARR_LDN: {
        uint8_t nidx = (op == RSVM_OP_ARR_LDN) ? rd_u8(vm) : 1;
        int32_t idxs[RSVM_ARR_MAX_DIM] = {0};
        for (int i = (int)nidx - 1; i >= 0; --i) idxs[i] = as_i32(pop(vm));
        rsvm_val_t arr = pop(vm);
        if (arr.ty != RSVM_TY_ARR) { vm->last_status = RSVM_ERR_TYPE; break; }
        uint8_t* meta_p = vm->heap + arr.v + sizeof(rsvm_obj_hdr_t);
        uint8_t ndim = meta_p[0];
        if (ndim < 1) ndim = 1;
        // row-major linear index
        int32_t lin = 0, stride = 1;
        for (int d = (int)ndim - 1; d >= 0; --d) {
            int32_t dim = meta_p[1 + d] ? meta_p[1 + d] : 1;
            int32_t ix = (d < nidx) ? idxs[d] : 0;
            if (ix < 0) ix = 0;
            if (ix >= dim) ix = dim - 1;
            lin += ix * stride;
            stride *= dim;
        }
        int32_t total = (int32_t)arr.aux;
        if (lin < 0 || lin >= total) { push(vm, V_i32(0)); break; }
        size_t meta = 1 + RSVM_ARR_MAX_DIM;
        rsvm_val_t* el = (rsvm_val_t*)(vm->heap + arr.v + sizeof(rsvm_obj_hdr_t) + meta);
        push(vm, el[lin]);
        break;
    }
    case RSVM_OP_ARR_ST:
    case RSVM_OP_ARR_STN: {
        uint8_t nidx = (op == RSVM_OP_ARR_STN) ? rd_u8(vm) : 1;
        rsvm_val_t val = pop(vm);
        int32_t idxs[RSVM_ARR_MAX_DIM] = {0};
        for (int i = (int)nidx - 1; i >= 0; --i) idxs[i] = as_i32(pop(vm));
        rsvm_val_t arr = pop(vm);
        if (arr.ty != RSVM_TY_ARR) { vm->last_status = RSVM_ERR_TYPE; break; }
        uint8_t* meta_p = vm->heap + arr.v + sizeof(rsvm_obj_hdr_t);
        uint8_t ndim = meta_p[0] ? meta_p[0] : 1;
        int32_t lin = 0, stride = 1;
        for (int d = (int)ndim - 1; d >= 0; --d) {
            int32_t dim = meta_p[1 + d] ? meta_p[1 + d] : 1;
            int32_t ix = (d < nidx) ? idxs[d] : 0;
            if (ix < 0) ix = 0;
            if (ix >= dim) ix = dim - 1;
            lin += ix * stride;
            stride *= dim;
        }
        if (lin < 0 || lin >= (int32_t)arr.aux) break;
        size_t meta = 1 + RSVM_ARR_MAX_DIM;
        rsvm_val_t* el = (rsvm_val_t*)(vm->heap + arr.v + sizeof(rsvm_obj_hdr_t) + meta);
        el[lin] = val;
        break;
    }
    case RSVM_OP_CONV: {
        uint8_t to = rd_u8(vm);
        rsvm_val_t a = pop(vm);
        if (to == RSVM_TY_F32) push(vm, V_f32(as_f32(a)));
        else if (to == RSVM_TY_F16) push(vm, V_f16_from_f32(as_f32(a)));
        else if (to == RSVM_TY_I32 || to == RSVM_TY_U32 || to == RSVM_TY_U16 || to == RSVM_TY_U8
                 || to == RSVM_TY_I16 || to == RSVM_TY_I8) {
            rsvm_val_t r; r.ty = to; r.aux = 0;
            r.v = is_float_ty(a.ty) ? (int32_t)as_f32(a) : a.v;
            push(vm, r);
        } else push(vm, a);
        break;
    }


    case RSVM_OP_SYS_CMD: {
        rsvm_val_t cmdv = pop(vm);
        char cmd[192];
        if (str_from_val(vm, cmdv, cmd, sizeof cmd) < 0 || !vm->host.sys_cmd) {
            push(vm, V_nil()); break;
        }
        char buf[256];
        int n = vm->host.sys_cmd(cmd, buf, (int)sizeof buf - 1, vm->host.user);
        if (n < 0) { push(vm, V_nil()); break; }
        push_cstr(vm, buf, n);
        break;
    }
    case RSVM_OP_SYS_EXEC: {
        rsvm_val_t pathv = pop(vm);
        char path[128];
        if (str_from_val(vm, pathv, path, sizeof path) < 0 || !vm->host.sys_exec) {
            push(vm, V_i32(-1)); break;
        }
        push(vm, V_i32(vm->host.sys_exec(path, vm->host.user)));
        break;
    }
    case RSVM_OP_SYS_OPEN: {
        rsvm_val_t nv = pop(vm);
        char name[64];
        if (str_from_val(vm, nv, name, sizeof name) < 0 || !vm->host.sys_open_app) {
            push(vm, V_i32(-1)); break;
        }
        push(vm, V_i32(vm->host.sys_open_app(name, vm->host.user)));
        break;
    }
    case RSVM_OP_SYS_LS: {
        rsvm_val_t pv = pop(vm);
        char path[128] = ".";
        if (pv.ty == RSVM_TY_STR) str_from_val(vm, pv, path, sizeof path);
        if (!vm->host.sys_ls) { push(vm, V_nil()); break; }
        char buf[384];
        int n = vm->host.sys_ls(path, buf, (int)sizeof buf - 1, vm->host.user);
        if (n < 0) { push(vm, V_nil()); break; }
        push_cstr(vm, buf, n);
        break;
    }
    case RSVM_OP_SYS_CD: {
        rsvm_val_t pv = pop(vm);
        char path[128];
        if (str_from_val(vm, pv, path, sizeof path) < 0 || !vm->host.sys_cd) {
            push(vm, V_i32(-1)); break;
        }
        push(vm, V_i32(vm->host.sys_cd(path, vm->host.user)));
        break;
    }
    case RSVM_OP_SYS_PWD: {
        if (!vm->host.sys_pwd) { push(vm, V_nil()); break; }
        char buf[128];
        int n = vm->host.sys_pwd(buf, (int)sizeof buf - 1, vm->host.user);
        if (n < 0) { push(vm, V_nil()); break; }
        push_cstr(vm, buf, n);
        break;
    }
    case RSVM_OP_MW_TEXT: {
        rsvm_val_t tv = pop(vm);
        rsvm_val_t wv = pop(vm);
        char text[192];
        if (str_from_val(vm, tv, text, sizeof text) < 0 || !vm->host.mw_set_text) {
            push(vm, V_i32(-1)); break;
        }
        push(vm, V_i32(vm->host.mw_set_text(as_i32(wv), text, vm->host.user)));
        break;
    }
    case RSVM_OP_UART_SEND: {
        rsvm_val_t data = pop(vm);
        rsvm_val_t tyv = pop(vm);
        if (data.ty != RSVM_TY_STR || !vm->host.uart_send) {
            push(vm, V_i32(-1)); break;
        }
        uint8_t ty = (uint8_t)as_i32(tyv);
        const uint8_t* bytes = (const uint8_t*)(vm->str_pool + data.v);
        int n = vm->host.uart_send(ty, bytes, (uint16_t)data.aux, vm->host.user);
        push(vm, V_i32(n));
        break;
    }
    case RSVM_OP_POOL_OP: {
        rsvm_val_t data = pop(vm);
        rsvm_val_t opv = pop(vm);
        rsvm_val_t namev = pop(vm);
        char name[64];
        char dbuf[256];
        int dlen = 0;
        if (str_from_val(vm, namev, name, sizeof name) < 0 || !vm->host.pool_op) {
            push(vm, V_nil()); break;
        }
        if (data.ty == RSVM_TY_STR)
            dlen = str_from_val(vm, data, dbuf, sizeof dbuf);
        char out[256];
        int n = vm->host.pool_op(name, as_i32(opv), dlen >= 0 ? dbuf : NULL, dlen,
                                 out, (int)sizeof out - 1, vm->host.user);
        if (n < 0) { push(vm, V_nil()); break; }
        push_cstr(vm, out, n);
        break;
    }
    case RSVM_OP_SYS_ENV: {
        rsvm_val_t kv = pop(vm);
        char key[64];
        if (str_from_val(vm, kv, key, sizeof key) < 0 || !vm->host.sys_env) {
            push(vm, V_nil()); break;
        }
        char buf[128];
        int n = vm->host.sys_env(key, buf, (int)sizeof buf - 1, vm->host.user);
        if (n < 0) { push(vm, V_nil()); break; }
        push_cstr(vm, buf, n);
        break;
    }


    case RSVM_OP_SYS_GREP: {
        rsvm_val_t pat = pop(vm);
        rsvm_val_t hay = pop(vm);
        char pattern[64], text[512];
        if (str_from_val(vm, pat, pattern, sizeof pattern) < 0 ||
            str_from_val(vm, hay, text, sizeof text) < 0) {
            push(vm, V_nil()); break;
        }
        // line-oriented substring match
        char out[384]; int o = 0;
        char* line = text;
        while (line && *line) {
            char* nl = strchr(line, '\n');
            size_t llen = nl ? (size_t)(nl - line) : strlen(line);
            char tmp[256];
            if (llen >= sizeof tmp) llen = sizeof tmp - 1;
            memcpy(tmp, line, llen); tmp[llen] = 0;
            if (strstr(tmp, pattern)) {
                if (o + (int)llen + 1 < (int)sizeof out) {
                    memcpy(out + o, tmp, llen); o += (int)llen;
                    out[o++] = '\n';
                }
            }
            if (!nl) break;
            line = nl + 1;
        }
        if (o > 0 && out[o-1] == '\n') o--;
        out[o] = 0;
        push_cstr(vm, out, o);
        break;
    }
    case RSVM_OP_SYS_CAT: {
        // same as FREAD — host file_read
        rsvm_val_t pathv = pop(vm);
        char path[128];
        if (str_from_val(vm, pathv, path, sizeof path) < 0 || !vm->host.file_read) {
            push(vm, V_nil()); break;
        }
        char buf[512];
        int n = vm->host.file_read(path, buf, (int)sizeof(buf) - 1, vm->host.user);
        if (n < 0) { push(vm, V_nil()); break; }
        push_cstr(vm, buf, n);
        break;
    }
    case RSVM_OP_SYS_ECHO: {
        rsvm_val_t s = pop(vm);
        if (s.ty == RSVM_TY_STR && vm->host.print_str)
            vm->host.print_str(vm->str_pool + s.v, s.aux, vm->host.user);
        else if (s.ty != RSVM_TY_STR)
            print_val(vm, s);
        push(vm, s);
        break;
    }
    case RSVM_OP_SYS_HEAD: {
        rsvm_val_t nv = pop(vm);
        rsvm_val_t tv = pop(vm);
        char text[512];
        if (str_from_val(vm, tv, text, sizeof text) < 0) {
            push(vm, V_nil()); break;
        }
        int nlines = as_i32(nv);
        if (nlines < 0) nlines = 0;
        char out[384]; int o = 0, lines = 0;
        for (char* p = text; *p && lines < nlines; ++p) {
            if (o < (int)sizeof(out) - 1) out[o++] = *p;
            if (*p == '\n') lines++;
        }
        out[o] = 0;
        push_cstr(vm, out, o);
        break;
    }
    case RSVM_OP_SYS_WC: {
        rsvm_val_t tv = pop(vm);
        char text[512];
        if (str_from_val(vm, tv, text, sizeof text) < 0) {
            push(vm, V_i32(0)); break;
        }
        int lines = 0;
        if (text[0]) {
            lines = 1;
            for (char* p = text; *p; ++p) if (*p == '\n') lines++;
        }
        push(vm, V_i32(lines));
        break;
    }
    case RSVM_OP_SYSCONF_GET: {
        rsvm_val_t kv = pop(vm);
        char key[64];
        if (str_from_val(vm, kv, key, sizeof key) < 0 || !vm->host.sysconf_get) {
            push(vm, V_nil()); break;
        }
        char buf[128];
        int n = vm->host.sysconf_get(key, buf, (int)sizeof buf - 1, vm->host.user);
        if (n < 0) { push(vm, V_nil()); break; }
        // if purely integer text, push i32
        char* end = NULL;
        long v = strtol(buf, &end, 10);
        if (end && end != buf && *end == 0) {
            push(vm, V_i32((int32_t)v));
        } else {
            push_cstr(vm, buf, n);
        }
        break;
    }
    case RSVM_OP_SYSCONF_SET: {
        rsvm_val_t val = pop(vm);
        rsvm_val_t kv = pop(vm);
        char key[64], vbuf[128];
        if (str_from_val(vm, kv, key, sizeof key) < 0 || !vm->host.sysconf_set) {
            push(vm, V_i32(-1)); break;
        }
        if (val.ty == RSVM_TY_STR) {
            str_from_val(vm, val, vbuf, sizeof vbuf);
        } else {
            snprintf(vbuf, sizeof vbuf, "%ld", (long)as_i32(val));
        }
        push(vm, V_i32(vm->host.sysconf_set(key, vbuf, vm->host.user)));
        break;
    }


    case RSVM_OP_NATIVE: {
        uint8_t nargs = rd_u8(vm);
        if (nargs > 8) nargs = 8;
        int32_t args[8];
        // stack: name, arg0, arg1, ... arg{n-1}  (args pushed after name)
        // emit order: name expr, then arg exprs, then NATIVE nargs
        // so pop args in reverse
        for (int i = (int)nargs - 1; i >= 0; --i) {
            args[i] = as_i32(pop(vm));
        }
        rsvm_val_t nv = pop(vm);
        char name[64];
        if (str_from_val(vm, nv, name, sizeof name) < 0 || !vm->host.native_call) {
            push(vm, V_i32(0)); break;
        }
        int32_t out = 0;
        int st = vm->host.native_call(name, args, (int)nargs, &out, vm->host.user);
        (void)st;
        push(vm, V_i32(out));
        break;
    }

    case RSVM_OP_IGNORE: {
        // soft pop – bare void calls (n_out==0) leave nothing on the stack
        if (vm->sp > 0) (void)pop(vm);
        break;
    }

    case RSVM_OP_STR_LIT: {
        uint8_t n = rd_u8(vm);
        if ((uint16_t)(vm->pc + n) > vm->code_len) { vm->last_status = RSVM_ERR_CODE; break; }
        int16_t off = str_intern(vm, (const char*)&vm->code[vm->pc], n);
        vm->pc = (uint16_t)(vm->pc + n);
        if (off < 0) { vm->last_status = RSVM_ERR_HEAP; break; }
        rsvm_val_t s; s.ty = RSVM_TY_STR; s.aux = n; s.v = off;
        push(vm, s);
        break;
    }
    case RSVM_OP_STR_CHAR: {
        rsvm_val_t idx = pop(vm);
        rsvm_val_t s = pop(vm);
        int32_t i = as_i32(idx);
        if (s.ty == RSVM_TY_ARR) {
            if (i < 0 || i >= (int32_t)s.aux) { push(vm, V_i32(0)); break; }
            size_t meta = 1 + RSVM_ARR_MAX_DIM;
            rsvm_val_t* el = (rsvm_val_t*)(vm->heap + s.v + sizeof(rsvm_obj_hdr_t) + meta);
            push(vm, el[i]);
            break;
        }
        if (s.ty != RSVM_TY_STR) { vm->last_status = RSVM_ERR_TYPE; break; }
        int32_t ch = 0;
        if (i >= 0 && i < (int32_t)s.aux &&
            (uint16_t)(s.v + i) < vm->str_pool_used)
            ch = (unsigned char)vm->str_pool[s.v + i];
        push(vm, V_i32(ch));
        break;
    }
    case RSVM_OP_STR_LEN: {
        rsvm_val_t s = pop(vm);
        if (s.ty != RSVM_TY_STR) { vm->last_status = RSVM_ERR_TYPE; break; }
        push(vm, V_i32((int32_t)s.aux));
        break;
    }
    case RSVM_OP_PRINT_C: {
        rsvm_val_t c = pop(vm);
        char ch = (char)(as_i32(c) & 0xFF);
        if (vm->host.print_char) vm->host.print_char(ch, vm->host.user);
        else if (vm->host.print_str) vm->host.print_str(&ch, 1, vm->host.user);
        break;
    }
    case RSVM_OP_PRINT_NL: {
        if (vm->host.print_char) vm->host.print_char('\n', vm->host.user);
        else if (vm->host.print_str) vm->host.print_str("\n", 1, vm->host.user);
        break;
    }

    default:
        vm->last_status = RSVM_ERR_CODE;
        vm->running = false;
        break;
    }
    return vm->last_status;
}

rsvm_status_t rsvm_run(rsvm_t* vm) {
    if (!vm) return RSVM_ERR_CODE;
    vm->running = true;
    vm->steps = 0;
    vm->pc = vm->entry;
    vm->sp = 0;
    vm->csp = 0;
    vm->frame_base = 0;
    vm->last_status = RSVM_OK;
    // Fast path: call step in a tight loop. Step-limit is checked inside step;
    // keep the call so monitors/hooks stay correct. For max speed use C backend.
    while (vm->running && vm->last_status == RSVM_OK)
        rsvm_step(vm);
    return vm->last_status;
}

// Optional: run with less frequent step-limit sampling (bench / trusted code)
rsvm_status_t rsvm_run_unchecked(rsvm_t* vm) {
    if (!vm) return RSVM_ERR_CODE;
    vm->running = true;
    vm->steps = 0;
    vm->pc = vm->entry;
    vm->sp = 0;
    vm->csp = 0;
    vm->frame_base = 0;
    vm->last_status = RSVM_OK;
    uint32_t limit = vm->step_limit;
    while (vm->running && vm->last_status == RSVM_OK) {
        // still count steps but only bail every 64 ops for branch prediction
        if ((vm->steps & 63u) == 0u && vm->steps >= limit) {
            vm->last_status = RSVM_ERR_STEP_LIMIT;
            vm->running = false;
            break;
        }
        rsvm_step(vm);
    }
    return vm->last_status;
}

uint16_t rsvm_disasm(const rsvm_t* vm, uint16_t pc, char* buf, size_t buf_len) {
    if (!vm || !buf || buf_len < 8 || pc >= vm->code_len) return 0;
    uint16_t start = pc;
    uint8_t op = vm->code[pc++];
    switch (op) {
    case RSVM_OP_HALT: snprintf(buf, buf_len, "HALT"); break;
    case RSVM_OP_CALL: snprintf(buf, buf_len, "CALL f%u", vm->code[pc]); pc++; break;
    case RSVM_OP_RET:  snprintf(buf, buf_len, "RET %u", vm->code[pc]); pc++; break;
    case RSVM_OP_RET0: snprintf(buf, buf_len, "RET0"); break;
    case RSVM_OP_NEW:  snprintf(buf, buf_len, "NEW s%u", vm->code[pc]); pc++; break;
    case RSVM_OP_ADD:  snprintf(buf, buf_len, "ADD"); break;
    case RSVM_OP_LOAD: snprintf(buf, buf_len, "LOAD s%u", vm->code[pc]); pc++; break;
    case RSVM_OP_STORE:snprintf(buf, buf_len, "STORE s%u", vm->code[pc]); pc++; break;
    default: snprintf(buf, buf_len, "OP_%02X", op); break;
    }
    return (uint16_t)(pc - start);
}

void rsvm_register_modules(rsvm_t* vm) {
    if (!vm) return;
    rsvm_mdl_immut_init(vm);
    rsvm_mdl_thread_init(vm);
    rsvm_mdl_property_init(vm);
    rsvm_mdl_flowmap_init(vm);
}

void rsvm_ref_note(rsvm_t* vm, const char* name, uint8_t kind, uint16_t line) {
    if (!vm || !name || !name[0]) return;
    for (uint8_t i = 0; i < vm->ref_count; ++i) {
        if (vm->refs[i].kind == kind &&
            strncmp(vm->refs[i].name, name, RSVM_NAME_LEN) == 0) {
            vm->refs[i].uses++;
            return;
        }
    }
    if (vm->ref_count >= RSVM_MAX_REFS) return;
    rsvm_ref_ent_t* r = &vm->refs[vm->ref_count++];
    memset(r, 0, sizeof(*r));
    strncpy(r->name, name, RSVM_NAME_LEN - 1);
    r->kind = kind;
    r->line = line;
    r->uses = 1;
}

// Minimal .bvul: magic "BVUL" + version + code_len + entry + step_limit
// + code bytes + str_pool_used + str_pool + func_count + funcs blob
size_t rsvm_save_bvul(const rsvm_t* vm, uint8_t* out, size_t out_cap) {
    if (!vm || !out) return 0;
    size_t need = 4 + 1 + 2 + 2 + 4 + vm->code_len + 2 + vm->str_pool_used
                + 1 + vm->func_count * sizeof(rsvm_func_ent_t);
    if (need > out_cap) return 0;
    size_t o = 0;
    out[o++] = 'B'; out[o++] = 'V'; out[o++] = 'U'; out[o++] = 'L';
    out[o++] = 1; // version
    out[o++] = (uint8_t)(vm->code_len & 0xFF);
    out[o++] = (uint8_t)(vm->code_len >> 8);
    out[o++] = (uint8_t)(vm->entry & 0xFF);
    out[o++] = (uint8_t)(vm->entry >> 8);
    uint32_t sl = vm->step_limit;
    memcpy(out + o, &sl, 4); o += 4;
    memcpy(out + o, vm->code, vm->code_len); o += vm->code_len;
    out[o++] = (uint8_t)(vm->str_pool_used & 0xFF);
    out[o++] = (uint8_t)(vm->str_pool_used >> 8);
    memcpy(out + o, vm->str_pool, vm->str_pool_used); o += vm->str_pool_used;
    out[o++] = vm->func_count;
    memcpy(out + o, vm->funcs, vm->func_count * sizeof(rsvm_func_ent_t));
    o += vm->func_count * sizeof(rsvm_func_ent_t);
    return o;
}

rsvm_status_t rsvm_load_bvul(rsvm_t* vm, const uint8_t* data, size_t len) {
    if (!vm || !data || len < 14) return RSVM_ERR_BAD_MAGIC;
    if (data[0] != 'B' || data[1] != 'V' || data[2] != 'U' || data[3] != 'L')
        return RSVM_ERR_BAD_MAGIC;
    rsvm_host_t host = vm->host;
    rsvm_init(vm);
    vm->host = host;
    size_t o = 5;
    uint16_t cl = (uint16_t)data[o] | ((uint16_t)data[o + 1] << 8); o += 2;
    uint16_t en = (uint16_t)data[o] | ((uint16_t)data[o + 1] << 8); o += 2;
    uint32_t sl; memcpy(&sl, data + o, 4); o += 4;
    if (cl > RSVM_MAX_CODE || o + cl > len) return RSVM_ERR_CODE;
    memcpy(vm->code, data + o, cl); o += cl;
    vm->code_len = cl;
    vm->entry = en;
    vm->step_limit = sl ? sl : RSVM_MAX_STEPS;
    if (o + 2 > len) return RSVM_ERR_CODE;
    uint16_t spu = (uint16_t)data[o] | ((uint16_t)data[o + 1] << 8); o += 2;
    if (spu > RSVM_STR_POOL || o + spu > len) return RSVM_ERR_CODE;
    memcpy(vm->str_pool, data + o, spu); o += spu;
    vm->str_pool_used = spu;
    if (o >= len) return RSVM_OK;
    uint8_t fc = data[o++];
    if (fc > RSVM_MAX_FUNCS || o + fc * sizeof(rsvm_func_ent_t) > len)
        return RSVM_ERR_CODE;
    memcpy(vm->funcs, data + o, fc * sizeof(rsvm_func_ent_t));
    vm->func_count = fc;
    return RSVM_OK;
}
