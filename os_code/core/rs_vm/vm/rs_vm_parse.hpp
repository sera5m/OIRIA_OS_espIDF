#pragma once
#include "rs_vm.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char message[96];
    int  line;
    int  column;
} rsvm_parse_err_t;

rsvm_status_t rsvm_compile(rsvm_t* vm, const char* source, rsvm_parse_err_t* err);
// Compile a root file path (enables include/import relative to its directory)
rsvm_status_t rsvm_compile_file(rsvm_t* vm, const char* path, rsvm_parse_err_t* err);
rsvm_status_t rsvm_eval(rsvm_t* vm, const char* source, rsvm_parse_err_t* err);
rsvm_status_t rsvm_eval_file(rsvm_t* vm, const char* path, rsvm_parse_err_t* err);

#ifdef __cplusplus
}
#endif
