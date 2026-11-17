#pragma once
#include <stdbool.h>

// Returns true if the line was a remote command and was handled.
// Called from the rshell terminal task for lines starting with '/'.
bool remote_handle_line(const char *line);
