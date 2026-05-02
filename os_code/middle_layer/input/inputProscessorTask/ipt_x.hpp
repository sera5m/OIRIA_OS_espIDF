#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "code_stuff/types.h"
#include "os_code/middle_layer/input/input_handler.hpp"

// Start the unified input task
void startInputTask();

// Global queue handle (still needed for callbacks to push events)
extern QueueHandle_t g_inputEventQueue;