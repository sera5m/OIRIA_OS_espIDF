// debugger/rs_dbg.cpp
#include "os_code/core/rs_vm/debugger/rs_dbg.hpp"
#include <stdio.h>
#include <string.h>

void rsvm_dbg_error(const rsvm_t* vm, int line, int col, const char* msg) {
    if (!vm) return;

    fprintf(stderr, "[rsvm] error: %s", rsvm_status_str(vm->last_status));

    if (vm->last_status == RSVM_ERR_STEP_LIMIT)
        fprintf(stderr, " (steps=%lu limit=%lu)", vm->steps, vm->step_limit);

    if (msg && msg[0])
        fprintf(stderr, " — line %d col %d: %s", line, col, msg);

    fputc('\n', stderr);
}

void rsvm_dbg_stats(const rsvm_t* vm, FILE* out) {
    if (!vm || !out) return;

    fprintf(out, "---- rsvm stats ----\n");
    fprintf(out, "  status     : %s\n", rsvm_status_str(vm->last_status));
    fprintf(out, "  steps      : %lu / %lu\n", vm->steps, vm->step_limit);

    fprintf(out, "  code_len   : %d\n", vm->code_len);
    fprintf(out, "  entry_pc   : %d\n", vm->entry);
    fprintf(out, "  slots      : %d\n", vm->slot_count);
    fprintf(out, "  funcs      : %d\n", vm->func_count);
    fprintf(out, "  structs    : %d\n", vm->struct_count);
    fprintf(out, "  enums      : %d\n", vm->enum_count);
    fprintf(out, "  flows      : %d\n", vm->flow_count);

    fprintf(out, "  heap_used  : %d / %lu\n",
            vm->heap_used, (unsigned long)RSVM_HEAP_BYTES);

    fprintf(out, "  str_pool   : %d / %lu\n",
            vm->str_pool_used, (unsigned long)RSVM_STR_POOL);

    fprintf(out, "  refs       : %d\n", vm->ref_count);

    fprintf(out, "  sp/csp     : %d / %d\n",
            (int)vm->sp, (int)vm->csp);

    fprintf(out, "-------------------\n");
}

void rsvm_dbg_refs(const rsvm_t* vm, FILE* out) {
    if (!vm || !out) return;

    fprintf(out, "---- ref table (%u) ----\n", vm->ref_count);
    fprintf(out, "  %-4s %-16s %-6s %-6s %s\n",
            "id", "name", "kind", "line", "uses");

    for (uint8_t i = 0; i < vm->ref_count; ++i) {
        const rsvm_ref_ent_t* r = &vm->refs[i];

        const char* k =
            r->kind == RSVM_REF_FUNC  ? "func" :
            r->kind == RSVM_REF_VAR   ? "var" :
            r->kind == RSVM_REF_FIELD ? "field" :
            "?";

        fprintf(out, "  %-4u %-16s %-6s %-6u %u\n",
                i, r->name, k, r->line, r->uses);
    }

    fprintf(out, "-----------------------\n");
}

void rsvm_dbg_monitors(const rsvm_t* vm, FILE* out) {
    if (!vm || !out) return;

    int any = 0;

    for (uint8_t i = 0; i < vm->func_count; ++i) {
        uint16_t p = vm->funcs[i].props;

        if (!(p & (RSVM_PROP_MON_TIME |
                   RSVM_PROP_MON_TIME_HI |
                   RSVM_PROP_MON_RAM |
                   RSVM_PROP_MON_EXECS)))
            continue;

        if (!any) {
            fprintf(out, "---- monitors ----\n");
            any = 1;
        }

        fprintf(out, "  fn %s:\n", vm->funcs[i].name);

        if (p & RSVM_PROP_MON_EXECS)
            fprintf(out, "    execs     : %lu\n", vm->func_execs[i]);

        if (p & RSVM_PROP_MON_TIME)
            fprintf(out, "    last_ms   : %lu\n", vm->func_last_ms[i]);

        if (p & RSVM_PROP_MON_TIME_HI)
            fprintf(out, "    last_us   : %lu\n", vm->func_last_us[i]);

        if (p & RSVM_PROP_MON_RAM)
            fprintf(out, "    last_ram  : %lu (heap+str delta)\n",
                    vm->func_last_ram[i]);
    }

    if (any)
        fprintf(out, "------------------\n");
}