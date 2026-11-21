// rshell_pump.hpp
#pragma once

#include "rshell_streamdefs.h"
#include "rshell_pipe.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <vector>

class DataStreamerPump {
public:
    static void start();
    static void stop();
    static bool running();
    static bool register_pipe(RshellPipe* pipe);
    static bool unregister_pipe(RshellPipe* pipe);
    // Pull queued InterProcess items for pipes that target this app
    // (or that have no specific target match) and deliver via on_stream_data.
    // Returns number of items delivered. Caller does not free them.
    static size_t drain_interprocess_for(AppBase* app, size_t max = 8);
    static bool pushInputEvent(const InputEvent& ev);
    // On success the pump owns item and will free it. On failure the caller still owns it.
    static bool pushDataItem(DataItem* item);

    static QueueHandle_t event_queue;

private:
    static void pump_task(void* param);
    static void dispatch_item(DataItem* item);
    static std::vector<RshellPipe*> copy_pipes();

    static std::vector<RshellPipe*> active_pipes;
};

extern DataStreamerPump gDataStreamer;
