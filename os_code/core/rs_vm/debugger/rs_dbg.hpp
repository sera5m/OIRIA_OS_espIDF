#pragma once
// =============================================================================
// debugger/rs_dbg – stats dump, error messenger, ref-table printer
// =============================================================================
#include "rs_vm.hpp"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Human-readable status (+ optional parse error line/col/message)
void rsvm_dbg_error(const rsvm_t* vm, int line, int col, const char* msg);

// Print run statistics (steps, heap, strings, funcs, refs, monitors)
void rsvm_dbg_stats(const rsvm_t* vm, FILE* out);

// Print compile-time reference table
void rsvm_dbg_refs(const rsvm_t* vm, FILE* out);

// Per-function monitor summary (exec counts, last time/ram)
void rsvm_dbg_monitors(const rsvm_t* vm, FILE* out);

#ifdef __cplusplus
}
#endif
