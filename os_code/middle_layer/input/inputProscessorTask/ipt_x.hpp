#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "code_stuff/types.h"
#include "os_code/core/rShell/s_hell.hpp"
//ooh le'ts see what now
#include "os_code/middle_layer/input/input_devs_agg.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"
extern QueueHandle_t ProcInputQueTarget;

void startInputHandlerTask();
// Forward declaration
class appManager;

// Global router function
void route_input_to_app_manager(const InputEvent& ev);