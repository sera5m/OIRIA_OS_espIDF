#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "code_stuff/types.h"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/middle_layer/input/hid_t.h"
// Start the unified input processing task
void startInputTask();

// NO queue here - using ProcInputQueTarget from main
// NO callbacks here - those are in input_handler you fucktard,
// do NOT move input hand ler into here holy fuck this set me back three days of work