#include "MS_watchapp.hpp"
#include "code_stuff/helperfunctions.hpp"
#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/core/notification_sys/rs_notif_dispatcher.h"



static const char* TAG = "MyWatchApp";

extern const char* months[];   // assume this exists globally

char time_str[256] = {0};


MyWatchApp::MyWatchApp(const ApplicationConfig& cfg) : AppBase(cfg) {
    appTickRateHZ = 20;
}

void MyWatchApp::on_start() {
    ESP_LOGI(TAG, "WatchApp started");

    watch_window = std::make_shared<Window>(
        WindowCfg{
            .Posx = 0, .Posy = 0,
            .Layer = 0, .renderPriority = 0,
            .win_width = static_cast<uint16_t>(v_env.clamped_screen_dim_w),
            .win_height = static_cast<uint16_t>(v_env.clamped_screen_dim_h),
            .win_rotation = 1,
            .AutoAlignment = false, .WrapText = true,
            .borderless = false, .ShowNameAtTopOfWindow = false,
            .TextSizeMult = 1,
            .BorderColor = 0x12FF, .BgColor = 0xAA00,
            .Bg_secondaryColor = 0xFF34, .WinTextColor = 0xFFFF,
            .backgroundType = BgFillType::waves,
            .UpdateRate = 1.0f
        }, "Watch"
    );

    WindowManager::getInstance().registerWindow(watch_window);
    bind_main_window(watch_window);
    on_draw();
}

void MyWatchApp::on_stop() {
    if (watch_window) {
        WindowManager::getInstance().unregisterWindow(watch_window);
        watch_window.reset();
    }
}

void MyWatchApp::on_pause()  { ESP_LOGI(TAG, "WatchApp paused"); }
void MyWatchApp::on_resume() { ESP_LOGI(TAG, "WatchApp resumed"); }

void MyWatchApp::next_mode() {
    current_mode_index = (current_mode_index + 1) % static_cast<int>(WM_COUNT);
    CurrentWatchMode = static_cast<WatchMode>(current_mode_index);
    on_draw();
}
void MyWatchApp::on_draw() {
    if (!watch_window) return;

    switch (CurrentWatchMode) {
        case WM_MAIN:       draw_main(); break;
        case WM_STOPWATCH:  draw_stopwatch(); break;
        case WM_ALARMS:     draw_alarms(); break;
        case WM_TIMER:      draw_timers(); break;
        case WM_NTP_SYNCH:  draw_ntp_sync(); break;
        case WM_SET_TIME:   draw_set_time(); break;
        default:            draw_main(); break;
    }
    watch_window->dirty = true;
}
void MyWatchApp::prev_mode() {
    current_mode_index = (current_mode_index + static_cast<int>(WM_COUNT) - 1) % static_cast<int>(WM_COUNT);
    CurrentWatchMode = static_cast<WatchMode>(current_mode_index);
    on_draw();
}

void MyWatchApp::draw_main() {
    int month_idx = std::max(0, std::min(11, v_env.displayTime.month - 1));
    snprintf(time_str, 256,
             "<|size=6|><|color=0xAD0F|>%02d:%02d:%02d<|n|><|size=2|>%s %d, %d",
             v_env.displayTime.hh, v_env.displayTime.mm, v_env.displayTime.ss,
             months[month_idx], v_env.displayTime.day, v_env.displayTime.year);
    watch_window->SetText(time_str);
}

void MyWatchApp::draw_stopwatch() {
    uint32_t total_sec = stopwatch_ms / 1000;
    uint32_t min = total_sec / 60;
    uint32_t sec = total_sec % 60;
    uint32_t ms  = (stopwatch_ms % 1000) / 10;

    snprintf(time_str, 256,
             "<|size=5|><|color=0x00FF00|>STOPWATCH<|n|>%02lu:%02lu.%02lu<|n|>"
             "<|size=2|>ENTER = %s", min, sec, ms,
             stopwatch_running ? "STOP" : "START");
    watch_window->SetText(time_str);
}

void MyWatchApp::draw_alarms() {
    std::string txt = "<|size=3|>ALARMS<|n|>";
    for (size_t i = 0; i < alarms.size() && i < 3; ++i) {
        if (!alarms[i].enabled) continue;
        char line[64];
        snprintf(line, sizeof(line), "%02d:%02d<|n|>", alarms[i].hh, alarms[i].mm);
        txt += line;
    }
    watch_window->SetText(txt.c_str());
}

void MyWatchApp::draw_timers() {
    std::string txt = "<|size=3|>TIMERS<|n|>";
    for (size_t i = 0; i < timers.size(); ++i) {
        uint32_t rem = timers[i].remaining_ms / 1000;
        uint32_t h = rem / 3600;
        uint32_t m = (rem % 3600) / 60;
        uint32_t s = rem % 60;
        char line[64];
        snprintf(line, sizeof(line), "%s %02lu:%02lu:%02lu<|n|>",
                 timers[i].running ? "[RUN]" : "[STOP]", h, m, s);
        txt += line;
    }
    watch_window->SetText(txt.c_str());
}

void MyWatchApp::draw_ntp_sync() {
    watch_window->SetText("<|size=4|>NTP SYNCH<|n|>Placeholder");
}

void MyWatchApp::draw_set_time() {
    watch_window->SetText("<|size=4|>SET TIME<|n|>Placeholder");
}

void MyWatchApp::tick_app(uint32_t delta_ms) {
    if (stopwatch_running) {
        stopwatch_ms += delta_ms;
    }

    for (auto& t : timers) {
        if (t.running && t.remaining_ms > delta_ms) {
            t.remaining_ms -= delta_ms;
        } else if (t.running) {
            t.remaining_ms = 0;
            t.running = false;
           // h_alert_dispatch(30, true, 100, true);
        }
    }

    static uint32_t accum = 0;
    accum += delta_ms;
    if (accum >= 200) {
        on_draw();
        accum = 0;
    }
}

void MyWatchApp::handle_enter() {
    switch (CurrentWatchMode) {
        case WM_STOPWATCH:
            stopwatch_running = !stopwatch_running;
            break;

        case WM_TIMER:
            if (selected_timer < (int)timers.size()) {
                auto& t = timers[selected_timer];
                t.running = !t.running;
                if (t.running && t.remaining_ms == 0) {
                    t.remaining_ms = 3600000; // 1 hour default
                }
            }
            break;

        default:
            break;
    }
    on_draw();
}

void MyWatchApp::handle_back_in_mode() {
    if (CurrentWatchMode == WM_MAIN) {
        appManager::instance().close_current_and_open("MenuApp");
    } else {
        CurrentWatchMode = WM_MAIN;
        current_mode_index = 0;
        on_draw();
    }
}

void MyWatchApp::receive_event_input(const void* event) {
    if (!event) return;
    
    const InputEvent* ev = static_cast<const InputEvent*>(event);

    switch (ev->action) {
        case KeyAction::Tap:
            switch (ev->key) {
                case KEY_LEFT:  prev_mode(); break;
                case KEY_RIGHT: next_mode(); break;
                case KEY_ENTER: handle_enter(); break;
                case KEY_BACK:  handle_back_in_mode(); break;
            }
            break;

        case KeyAction::Hold:
        case KeyAction::Release:
        case KeyAction::Repeat:
        case KeyAction::Unknown:
        case KeyAction::PositionDelta:
            // ignored for now
            break;
    }
}

void MyWatchApp::suspend()  { on_pause(); }
void MyWatchApp::force_close() {
    on_stop();
    stop_task();
}