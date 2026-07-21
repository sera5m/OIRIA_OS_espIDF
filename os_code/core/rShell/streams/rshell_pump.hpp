// rshell_pump.hpp
#pragma once

#include "rshell_streamdefs.h"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "rshell_pipe.hpp"  // Add this
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// REMOVE this forward declaration - it's defined in rshell_pool.hpp
// namespace psram {
//     struct EventRingBuffer;  // ← DELETE THIS
// }

class DataStreamerPump {
public:


    static void start();
    static void stop();
    static bool register_pipe(RshellPipe* pipe);
    static bool pushInputEvent(const InputEvent& ev);
    static bool pushDataItem(DataItem* item);


    static QueueHandle_t event_queue;
    static std::vector<RshellPipe*> active_pipes;  // pipe piep epiepiepiepie what are we linked to targets

private:
    static void pump_task(void* param);

};

extern DataStreamerPump gDataStreamer;
