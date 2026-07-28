#pragma once
#include <stdint.h>
#include "esp_timer.h"
#include <string>
#include <memory>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "os_code/core/window_env/wenv_basicThemes.h"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/middle_layer/input/hid_t.h"
//lol

// Forward declarations
struct InputEvent;

extern char time_str[256];
extern const char* months[];

typedef enum {
    WM_MAIN,
    WM_STOPWATCH,
    WM_ALARMS,
    WM_TIMER,
    WM_NTP_SYNCH,
    WM_SET_TIME,
    WM_SET_TIMEZONE,
    WM_COUNT
} WatchMode;

class MyWatchApp : public AppBase {
public:
    explicit MyWatchApp(const ApplicationConfig& cfg);

    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void on_draw() override;

    void on_start() override;
    void on_stop() override;
    void on_pause() override;
    void on_resume() override;

    void watchapp_back();

private:
    std::shared_ptr<Window> watch_window;

    WatchMode CurrentWatchMode = WM_MAIN;
    int current_mode_index = 0;

    // Stopwatch
    uint32_t stopwatch_ms = 0;
    bool stopwatch_running = false;

    // Timers
    struct Timer {
        uint32_t remaining_ms = 0;
        bool running = false;
    };
    std::vector<Timer> timers{3};
    int selected_timer = 0;

    // Alarms
    struct Alarm {
        uint8_t hh = 8, mm = 0;
        bool enabled = true;
    };
    std::vector<Alarm> alarms{4};

    void next_mode();
    void prev_mode();
    void handle_enter();
    void handle_back_in_mode();

    void draw_main();
    void draw_stopwatch();
    void draw_alarms();
    void draw_timers();
    void draw_ntp_sync();
    void draw_set_time();
};

// Registration function - call from main
void register_watch();