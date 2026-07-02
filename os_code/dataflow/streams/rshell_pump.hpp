#pragma once

#include "rshell_streamdefs.h"
#include "os_code/middle_layer/input/input_handler.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// PSRAM FIFO for high-rate events (decouples input from pump)
namespace psram {
    struct EventRingBuffer;  // defined in .cpp
}

class DataStreamerPump {
public:
    static void start();                     // creates pump task + inits
    static void stop();

    // Push from input task (non-blocking)
    static bool pushInputEvent(const InputEvent& ev);

    // General push (sensors, etc.)
    static bool pushDataItem(DataItem* item);   // takes ownership

private:
    static void pump_task(void* param);
    static QueueHandle_t event_queue;           // main queue to pump
};

extern DataStreamerPump gDataStreamer;