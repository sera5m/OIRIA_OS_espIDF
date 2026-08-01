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

// ULP (ESP32-S3 RISC-V) shared RTC symbols.
// After the ESP-IDF ULP build step the symbols appear as ulp_hour, ulp_minute,
// ulp_wake_main_now, ulp_timer0_remain_min, ulp_alarm0_hh, etc. (often as uint32_t).
extern "C" {
    #include "ulp_main.h"
}

// Fallbacks if the generated ULP header does not carry these macros
#ifndef ULP_TIMER_COUNT
#define ULP_TIMER_COUNT 3
#endif
#ifndef ULP_ALARM_COUNT
#define ULP_ALARM_COUNT 4
#endif
#ifndef ULP_WAKE_PERIODIC
#define ULP_WAKE_PERIODIC  (1u << 0)
#define ULP_WAKE_ALARM     (1u << 1)
#define ULP_WAKE_TIMER     (1u << 2)
#define ULP_WAKE_MANUAL    (1u << 3)
#endif

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

// Sub-field being edited inside set-time / set-timezone / alarm modes
typedef enum {
    EDIT_NONE = 0,
    EDIT_HOUR,
    EDIT_MINUTE,
    EDIT_SECOND,   // only used for set-time
    EDIT_TZ_HOUR,  // timezone offset hours
    EDIT_TZ_MIN,   // timezone offset minutes (0 or 30 typically)
    EDIT_ALARM_HH,
    EDIT_ALARM_MM,
    EDIT_TIMER_H,
    EDIT_TIMER_M,
    EDIT_TIMER_S
} EditField;

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

    // ---- Stopwatch ----
    uint32_t stopwatch_ms = 0;
    bool stopwatch_running = false;

    // ---- Timers (3 independent) ----
    struct Timer {
        uint32_t remaining_ms = 0;
        bool running = false;
        bool finished = false;   // latched until user clears
    };
    std::vector<Timer> timers{3};
    int selected_timer = 0;

    // ---- Alarms (4 slots) ----
    struct Alarm {
        uint8_t hh = 8;
        uint8_t mm = 0;
        bool enabled = false;
        bool triggered = false;  // latched for the current minute
    };
    std::vector<Alarm> alarms{4};
    int selected_alarm = 0;

    // ---- Set-time working copy (written to v_env + ULP on confirm) ----
    uint8_t edit_hh = 0;
    uint8_t edit_mm = 0;
    uint8_t edit_ss = 0;
    EditField edit_field = EDIT_NONE;

    // ---- Timezone offset (hours + minutes, signed) ----
    int8_t  tz_offset_h = 0;   // -12 … +14
    int8_t  tz_offset_m = 0;   // 0 or 30

    // ---- NTP status (simple state machine) ----
    enum class NtpState : uint8_t { Idle, Syncing, Success, Failed };
    NtpState ntp_state = NtpState::Idle;
    uint32_t ntp_last_attempt_ms = 0;

    // ---- ULP sync helpers ----
    void sync_from_ulp();          // read hour/minute from ULP into v_env.displayTime
    void sync_to_ulp();            // write current hh:mm to ULP RTC vars
    void apply_edited_time();      // push edit_* into v_env + ULP
    void check_alarms();           // fire any matching enabled alarm

    // Mode navigation
    void next_mode();
    void prev_mode();
    void handle_enter();
    void handle_back_in_mode();
    void handle_up();
    void handle_down();

    // Drawing
    void draw_main();
    void draw_stopwatch();
    void draw_alarms();
    void draw_timers();
    void draw_ntp_sync();
    void draw_set_time();
    void draw_set_timezone();
};

// Registration function – call from main
void register_watch();
