// rshell_pump.hpp
#pragma once

#include "rshell_streamdefs.h"
#include "rshell_pipe.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// REMOVE this line:
// namespace psram { struct EventRingBuffer; }  // ← DELETE

class DataStreamerPump {
public:
    static void start();
    static void stop();
    static bool register_pipe(RshellPipe* pipe);
    static bool pushInputEvent(const InputEvent& ev);
    static bool pushDataItem(DataItem* item);

    static QueueHandle_t event_queue;
    static std::vector<RshellPipe*> active_pipes;

private:
    static void pump_task(void* param);
};

extern DataStreamerPump gDataStreamer;