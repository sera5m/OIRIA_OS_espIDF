#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "code_stuff/types.h"

//ooh le'ts see what now

extern QueueHandle_t ProcInputQueTarget;

void startInputHandlerTask();