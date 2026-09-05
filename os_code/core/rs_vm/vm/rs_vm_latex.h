#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int rsvm_latex_to_vulcan(const char* latex, char* out, size_t cap);
#ifdef __cplusplus
}
#endif
