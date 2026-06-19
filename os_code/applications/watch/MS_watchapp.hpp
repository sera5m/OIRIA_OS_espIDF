#pragma once
#include <stdint.h>
#include "esp_timer.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include <string>
#include <memory>
#include <sstream>
#include <algorithm>
#include <variant>
#include "code_stuff/types.h"
#include <math.h>
#include "hardware/wiring/wiring.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "rom/cache.h"
#include <string.h>
#include "hardware/drivers/abstraction_layers/al_scr.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "os_code/core/window_env/wenv_basicThemes.h"
#include <vector>
#include "../../../hardware/drivers/psram_std/psram_std.hpp"
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/core/rShell/s_hell.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "tusb.h"
#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/core/notification_sys/rs_notif_dispatcher.h"



// Forward declaration
extern char time_str[256];   // Fixed size declaration
extern const char* months[];

extern void h_alert_dispatch(uint16_t duration_s, bool run_even_when_sleep, uint8_t loudness, bool useBuzzer);

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
    void suspend() override;
    void force_close() override;

    void on_start() override;
    void on_stop() override;
    void on_pause() override;
    void on_resume() override;
    void on_draw() override;

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
    std::vector<Timer> timers{3};  // 3 default timers
    int selected_timer = 0;
    bool timer_edit_mode = false;

    // Alarms (basic)
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

static ApplicationConfig make_watch_config() {
    ApplicationConfig cfg;
    cfg.capabilities = static_cast<uint32_t>(AppCapability::FULLSCREEN) |
                       static_cast<uint32_t>(AppCapability::NEEDS_WINDOW);
    cfg.stack_size_bytes = 8192;
    cfg.priority = 5;
    cfg.name = "WatchApp";
    cfg.tick_rate_hz = 20;
    return cfg;
}