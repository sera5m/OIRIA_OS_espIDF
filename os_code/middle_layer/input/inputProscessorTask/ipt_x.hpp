#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "code_stuff/types.h"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/middle_layer/input/hid_t.h"
#include "tusb.h"

// Forward declaration of InputEvent to break circular include


// Forward declaration
struct InputEvent;

void startInputTask();

// NO queue here - using ProcInputQueTarget from main
// NO callbacks here - those are in input_handler you fucktard dev
// do NOT move input hand ler into here holy fuck this set me back three days of work

//this is an external function and we have to declare this here to access it apparently
void RouteInput_HidTarget(InputEvent i_ev);