#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "code_stuff/types.h"

// Start the unified input processing task
void startInputTask();

// NO queue here - using ProcInputQueTarget from main
// NO callbacks here - those are in input_handler you fucktard,
// do NOT move input hand ler into here holy fuck this set me back three days of work