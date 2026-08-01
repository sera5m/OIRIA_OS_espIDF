#include "MS_watchapp.hpp"
#include "code_stuff/helperfunctions.hpp"
#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"
#include "os_code/core/notification_sys/rs_notif_dispatcher.h"
#include "os_code/core/rShell/rshell_appFramework.hpp"

static const char* TAG = "MyWatchApp";

extern const char* months[];

char time_str[256] = {0};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
MyWatchApp::MyWatchApp(const ApplicationConfig& cfg) : AppBase(cfg) {
    appTickRateHZ = 20;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void MyWatchApp::on_start() {
    ESP_LOGI(TAG, "WatchApp started");

    // Pull the latest time the ULP has been keeping while we were asleep
    sync_from_ulp();

    watch_window = std::make_shared<Window>(
        WindowCfg{
            .Posx = 0, .Posy = 0,
            .Layer = 0, .renderPriority = 0,
            .win_width  = 280,//static_cast<uint16_t>(v_env.clamped_screen_dim_w),
            .win_height = 240,//static_cast<uint16_t>(v_env.clamped_screen_dim_h),
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
    // Push current time back to ULP so it can keep counting while we sleep
    sync_to_ulp();

    if (watch_window) {
        WindowManager::getInstance().unregisterWindow(watch_window);
        watch_window.reset();
    }
}

void MyWatchApp::on_pause()  { ESP_LOGI(TAG, "WatchApp paused");  sync_to_ulp(); }
void MyWatchApp::on_resume() { ESP_LOGI(TAG, "WatchApp resumed"); sync_from_ulp(); }

// ---------------------------------------------------------------------------
// ULP ↔ main-core time / timer / alarm sync
// ESP-IDF renames the RTC symbols with an "ulp_" prefix after the ULP build.
// ---------------------------------------------------------------------------
void MyWatchApp::sync_from_ulp() {
    // Wall clock (ULP keeps HH:MM; seconds stay under main-core control)
    // Casts: IDF export often widens symbols to uint32_t.
    v_env.displayTime.hh = (uint8_t)ulp_hour;
    v_env.displayTime.mm = (uint8_t)ulp_minute;

    // Individual timer symbols (not arrays – IDF flattens arrays)
    const uint16_t trems[3] = {
        (uint16_t)ulp_timer0_remain_min,
        (uint16_t)ulp_timer1_remain_min,
        (uint16_t)ulp_timer2_remain_min
    };
    const uint8_t trun = (uint8_t)ulp_timer_running;

    for (int i = 0; i < 3 && i < (int)timers.size(); i++) {
        if (trun & (1u << i)) {
            timers[i].remaining_ms = (uint32_t)trems[i] * 60u * 1000u;
            timers[i].running = true;
            timers[i].finished = false;
        } else if (trems[i] == 0 && timers[i].running) {
            timers[i].remaining_ms = 0;
            timers[i].running = false;
            timers[i].finished = true;
        }
    }

    // Individual alarm symbols
    const uint8_t a_hh[4] = {
        (uint8_t)ulp_alarm0_hh, (uint8_t)ulp_alarm1_hh,
        (uint8_t)ulp_alarm2_hh, (uint8_t)ulp_alarm3_hh
    };
    const uint8_t a_mm[4] = {
        (uint8_t)ulp_alarm0_mm, (uint8_t)ulp_alarm1_mm,
        (uint8_t)ulp_alarm2_mm, (uint8_t)ulp_alarm3_mm
    };
    const uint8_t afired = (uint8_t)ulp_alarm_fired;

    for (int i = 0; i < 4 && i < (int)alarms.size(); i++) {
        if (afired & (1u << i)) {
            alarms[i].triggered = true;
            ESP_LOGI(TAG, "ULP alarm %d fired (%02u:%02u)", i,
                     (unsigned)a_hh[i], (unsigned)a_mm[i]);
        }
    }
    ulp_alarm_fired = 0;

    if (ulp_wake_main_now) {
        ulp_wake_main_now = false;
        ESP_LOGI(TAG, "Woken by ULP (reason=0x%02x) – time %02u:%02u",
                 (unsigned)ulp_wake_reason,
                 (unsigned)ulp_hour, (unsigned)ulp_minute);
        ulp_wake_reason = 0;
    }
}

void MyWatchApp::sync_to_ulp() {
    ulp_hour   = v_env.displayTime.hh;
    ulp_minute = v_env.displayTime.mm;

    // Push timers (individual symbols; IDF may widen them to uint32_t)
    uint8_t tmask = 0;
    auto mins_of = [&](int i) -> uint32_t {
        if (i >= (int)timers.size() || !timers[i].running || timers[i].remaining_ms == 0)
            return 0;
        uint32_t m = (timers[i].remaining_ms + 59999u) / 60000u;
        return (m > 0xFFFF) ? 0xFFFF : m;
    };
    {
        uint32_t m0 = mins_of(0); ulp_timer0_remain_min = m0; if (m0) tmask |= 1;
        uint32_t m1 = mins_of(1); ulp_timer1_remain_min = m1; if (m1) tmask |= 2;
        uint32_t m2 = mins_of(2); ulp_timer2_remain_min = m2; if (m2) tmask |= 4;
    }
    ulp_timer_running = tmask;

    // Push alarms (individual symbols)
    uint8_t amask = 0;
    if (alarms.size() > 0) {
        ulp_alarm0_hh = alarms[0].hh; ulp_alarm0_mm = alarms[0].mm;
        if (alarms[0].enabled) amask |= 1;
    }
    if (alarms.size() > 1) {
        ulp_alarm1_hh = alarms[1].hh; ulp_alarm1_mm = alarms[1].mm;
        if (alarms[1].enabled) amask |= 2;
    }
    if (alarms.size() > 2) {
        ulp_alarm2_hh = alarms[2].hh; ulp_alarm2_mm = alarms[2].mm;
        if (alarms[2].enabled) amask |= 4;
    }
    if (alarms.size() > 3) {
        ulp_alarm3_hh = alarms[3].hh; ulp_alarm3_mm = alarms[3].mm;
        if (alarms[3].enabled) amask |= 8;
    }
    ulp_alarm_enabled = amask;

    ESP_LOGD(TAG, "Pushed to ULP: %02u:%02u  timers=0x%02x  alarms=0x%02x",
             (unsigned)ulp_hour, (unsigned)ulp_minute,
             (unsigned)tmask, (unsigned)amask);
}

void MyWatchApp::apply_edited_time() {
    v_env.displayTime.hh = edit_hh;
    v_env.displayTime.mm = edit_mm;
    v_env.displayTime.ss = edit_ss;
    sync_to_ulp();
    ESP_LOGI(TAG, "Time set to %02u:%02u:%02u (ULP updated)", edit_hh, edit_mm, edit_ss);
}

// ---------------------------------------------------------------------------
// Alarm checker (called from tick)
// ---------------------------------------------------------------------------
void MyWatchApp::check_alarms() {
    const uint8_t now_h = v_env.displayTime.hh;
    const uint8_t now_m = v_env.displayTime.mm;

    for (auto& a : alarms) {
        if (!a.enabled) {
            a.triggered = false;
            continue;
        }
        if (a.hh == now_h && a.mm == now_m) {
            if (!a.triggered) {
                a.triggered = true;
                // Fire a short haptic / notification
                // h_alert_dispatch(30, true, 150, true);
                ESP_LOGI(TAG, "ALARM %02u:%02u fired", a.hh, a.mm);
            }
        } else {
            a.triggered = false;   // allow re-trigger next day
        }
    }
}

// ---------------------------------------------------------------------------
// Mode navigation
// ---------------------------------------------------------------------------
void MyWatchApp::next_mode() {
    // Leaving an edit mode without confirming discards the edit buffer
    edit_field = EDIT_NONE;
    current_mode_index = (current_mode_index + 1) % static_cast<int>(WM_COUNT);
    CurrentWatchMode = static_cast<WatchMode>(current_mode_index);

    // Seed edit buffer when entering set-time
    if (CurrentWatchMode == WM_SET_TIME) {
        edit_hh = v_env.displayTime.hh;
        edit_mm = v_env.displayTime.mm;
        edit_ss = v_env.displayTime.ss;
        edit_field = EDIT_HOUR;
    } else if (CurrentWatchMode == WM_SET_TIMEZONE) {
        edit_field = EDIT_TZ_HOUR;
    } else if (CurrentWatchMode == WM_ALARMS) {
        edit_field = EDIT_NONE;
        selected_alarm = 0;
    } else if (CurrentWatchMode == WM_TIMER) {
        edit_field = EDIT_NONE;
        selected_timer = 0;
    }
    on_draw();
}

void MyWatchApp::prev_mode() {
    edit_field = EDIT_NONE;
    current_mode_index = (current_mode_index + static_cast<int>(WM_COUNT) - 1) % static_cast<int>(WM_COUNT);
    CurrentWatchMode = static_cast<WatchMode>(current_mode_index);

    if (CurrentWatchMode == WM_SET_TIME) {
        edit_hh = v_env.displayTime.hh;
        edit_mm = v_env.displayTime.mm;
        edit_ss = v_env.displayTime.ss;
        edit_field = EDIT_HOUR;
    } else if (CurrentWatchMode == WM_SET_TIMEZONE) {
        edit_field = EDIT_TZ_HOUR;
    } else if (CurrentWatchMode == WM_ALARMS) {
        edit_field = EDIT_NONE;
        selected_alarm = 0;
    } else if (CurrentWatchMode == WM_TIMER) {
        edit_field = EDIT_NONE;
        selected_timer = 0;
    }
    on_draw();
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void MyWatchApp::on_draw() {
    if (!watch_window) return;

    switch (CurrentWatchMode) {
        case WM_MAIN:         draw_main(); break;
        case WM_STOPWATCH:    draw_stopwatch(); break;
        case WM_ALARMS:       draw_alarms(); break;
        case WM_TIMER:        draw_timers(); break;
        case WM_NTP_SYNCH:    draw_ntp_sync(); break;
        case WM_SET_TIME:     draw_set_time(); break;
        case WM_SET_TIMEZONE: draw_set_timezone(); break;
        default:              draw_main(); break;
    }
    watch_window->dirty = true;
}

void MyWatchApp::draw_main() {
    int month_idx = std::max(0, std::min(11, v_env.displayTime.month - 1));
    snprintf(time_str, sizeof(time_str),
             "<|n|><|n|><|size=4|><|color=0xAD0F|>%02d:%02d:%02d<|n|>"
             "<|size=2|>%s %d, %d<|n|>"
             "<|size=1|><|color=0x8888|>L/R=modes  BACK=menu",
             v_env.displayTime.hh, v_env.displayTime.mm, v_env.displayTime.ss,
             months[month_idx], v_env.displayTime.day, v_env.displayTime.year);
    watch_window->SetText(time_str);
}

void MyWatchApp::draw_stopwatch() {
    uint32_t total_sec = stopwatch_ms / 1000;
    uint32_t min = total_sec / 60;
    uint32_t sec = total_sec % 60;
    uint32_t cs  = (stopwatch_ms % 1000) / 10;   // centiseconds

    snprintf(time_str, sizeof(time_str),
             "<|n|><|n|><|size=4|><|color=0x00FF00|>STOPWATCH<|n|>"
             "<|n|><|size=4|>%02lu:%02lu.%02lu<|n|>"
             "<|size=2|>ENTER=%s  HOLD-ENTER=RESET<|n|>"
             "<|size=1|><|color=0x8888|>L/R=modes",
             (unsigned long)min, (unsigned long)sec, (unsigned long)cs,
             stopwatch_running ? "STOP" : "START");
    watch_window->SetText(time_str);
}

void MyWatchApp::draw_alarms() {
    std::string txt = "<|n|><|size=3|><|color=0xFFFF|>ALARMS<|n|>";

    for (size_t i = 0; i < alarms.size(); ++i) {
        char line[80];
        const bool sel = ((int)i == selected_alarm);
        const char* mark = sel ? ">" : " ";
        const char* en   = alarms[i].enabled ? "ON " : "OFF";

        if (sel && (edit_field == EDIT_ALARM_HH || edit_field == EDIT_ALARM_MM)) {
            // Highlight the field being edited
            if (edit_field == EDIT_ALARM_HH) {
                snprintf(line, sizeof(line),
                         "%s <|color=0xFDFC|>%02d<|color=0xFFFF|>:%02d  %s<|n|>",
                         mark, alarms[i].hh, alarms[i].mm, en);
            } else {
                snprintf(line, sizeof(line),
                         "%s %02d:<|color=0xFDFC|>%02d<|color=0xFFFF|>  %s<|n|>",
                         mark, alarms[i].hh, alarms[i].mm, en);
            }
        } else {
            snprintf(line, sizeof(line),
                     "%s %02d:%02d  %s<|n|>",
                     mark, alarms[i].hh, alarms[i].mm, en);
        }
        txt += line;
    }

    txt += "<|n|><|size=1|><|color=0x8888|>"
           "UP/DN=select  ENTER=edit/toggle  L/R=modes";
    watch_window->SetText(txt.c_str());
}

void MyWatchApp::draw_timers() {
    std::string txt = "<|n|><|size=3|><|color=0xFFFF|>TIMERS<|n|>";

    for (size_t i = 0; i < timers.size(); ++i) {
        uint32_t rem = timers[i].remaining_ms / 1000;
        uint32_t h = rem / 3600;
        uint32_t m = (rem % 3600) / 60;
        uint32_t s = rem % 60;

        char line[96];
        const bool sel = ((int)i == selected_timer);
        const char* mark = sel ? ">" : " ";
        const char* st = timers[i].finished ? "DONE" :
                         (timers[i].running ? "RUN " : "STOP");

        if (sel && (edit_field == EDIT_TIMER_H || edit_field == EDIT_TIMER_M || edit_field == EDIT_TIMER_S)) {
            // Show editable fields
            snprintf(line, sizeof(line),
                     "%s %s %s%02lu%s:%s%02lu%s:%s%02lu%s<|n|>",
                     mark, st,
                     edit_field == EDIT_TIMER_H ? "<|color=0xFDFC|>" : "",
                     (unsigned long)h,
                     edit_field == EDIT_TIMER_H ? "<|color=0xFFFF|>" : "",
                     edit_field == EDIT_TIMER_M ? "<|color=0xFDFC|>" : "",
                     (unsigned long)m,
                     edit_field == EDIT_TIMER_M ? "<|color=0xFFFF|>" : "",
                     edit_field == EDIT_TIMER_S ? "<|color=0xFDFC|>" : "",
                     (unsigned long)s,
                     edit_field == EDIT_TIMER_S ? "<|color=0xFFFF|>" : "");
        } else {
            snprintf(line, sizeof(line),
                     "%s %s %02lu:%02lu:%02lu<|n|>",
                     mark, st, (unsigned long)h, (unsigned long)m, (unsigned long)s);
        }
        txt += line;
    }

    txt += "<|n|><|size=1|><|color=0x8888|>"
           "UP/DN=select  ENTER=start/edit  L/R=modes";
    watch_window->SetText(txt.c_str());
}

void MyWatchApp::draw_ntp_sync() {
    const char* status = "Idle";
    switch (ntp_state) {
        case NtpState::Syncing:  status = "Syncing…"; break;
        case NtpState::Success:  status = "OK – time updated"; break;
        case NtpState::Failed:   status = "Failed"; break;
        default: break;
    }

    snprintf(time_str, sizeof(time_str),
             "<|n|><|size=3|><|color=0xFFFF|>NTP SYNC<|n|>"
             "<|size=2|>%s<|n|>"
             "<|size=1|><|color=0x8888|>ENTER=start  L/R=modes",
             status);
    watch_window->SetText(time_str);
}

void MyWatchApp::draw_set_time() {
    // Highlight the field currently under edit
    const char* h_col = (edit_field == EDIT_HOUR)   ? "<|color=0xFDFC|>" : "<|color=0xFFFF|>";
    const char* m_col = (edit_field == EDIT_MINUTE) ? "<|color=0xFDFC|>" : "<|color=0xFFFF|>";
    const char* s_col = (edit_field == EDIT_SECOND) ? "<|color=0xFDFC|>" : "<|color=0xFFFF|>";

    snprintf(time_str, sizeof(time_str),
             "<|n|><|size=3|>SET TIME<|n|>"
             "<|n|><|size=4|>%s%02u%s:%s%02u%s:%s%02u%s<|n|>"
             "<|size=1|><|color=0x8888|>"
             "UP/DN=change  ENTER=next field  HOLD-ENTER=save",
             h_col, edit_hh, "<|color=0xFFFF|>",
             m_col, edit_mm, "<|color=0xFFFF|>",
             s_col, edit_ss, "<|color=0xFFFF|>");
    watch_window->SetText(time_str);
}

void MyWatchApp::draw_set_timezone() {
    const char* h_col = (edit_field == EDIT_TZ_HOUR) ? "<|color=0xFDFC|>" : "<|color=0xFFFF|>";
    const char* m_col = (edit_field == EDIT_TZ_MIN)  ? "<|color=0xFDFC|>" : "<|color=0xFFFF|>";

    char sign = (tz_offset_h >= 0) ? '+' : '-';
    int abs_h = (tz_offset_h >= 0) ? tz_offset_h : -tz_offset_h;

    snprintf(time_str, sizeof(time_str),
             "<|size=3|>TIMEZONE<|n|>"
             "<|n|><|size=4|>UTC%s%s%02d%s:%s%02d%s<|n|>"
             "<|size=1|><|color=0x8888|>"
             "UP/DN=change  ENTER=next  HOLD-ENTER=save",
             (sign == '+' ? "+" : "-"),
             h_col, abs_h, "<|color=0xFFFF|>",
             m_col, (int)tz_offset_m, "<|color=0xFFFF|>");
    watch_window->SetText(time_str);
}

// ---------------------------------------------------------------------------
// Tick – stopwatch, timers, alarm check, periodic ULP pull, redraw
// ---------------------------------------------------------------------------
void MyWatchApp::tick_app(uint32_t delta_ms) {
    // Stopwatch
    if (stopwatch_running) {
        stopwatch_ms += delta_ms;
    }

    // Timers
    for (auto& t : timers) {
        if (t.running) {
            if (t.remaining_ms > delta_ms) {
                t.remaining_ms -= delta_ms;
            } else {
                t.remaining_ms = 0;
                t.running = false;
                t.finished = true;
                // h_alert_dispatch(30, true, 100, true);
                ESP_LOGI(TAG, "Timer finished");
            }
        }
    }

    // Alarms (once per second is enough)
    static uint32_t alarm_accum = 0;
    alarm_accum += delta_ms;
    if (alarm_accum >= 1000) {
        alarm_accum = 0;
        check_alarms();
    }

    // Pull ULP time every ~5 s (cheap, keeps display accurate after deep sleep)
    static uint32_t ulp_accum = 0;
    ulp_accum += delta_ms;
    if (ulp_accum >= 5000) {
        ulp_accum = 0;
        sync_from_ulp();
    }

    // Redraw ~5 Hz
    static uint32_t draw_accum = 0;
    draw_accum += delta_ms;
    if (draw_accum >= 200) {
        draw_accum = 0;
        on_draw();
    }
}

// ---------------------------------------------------------------------------
// Input helpers
// ---------------------------------------------------------------------------
void MyWatchApp::handle_enter() {
    switch (CurrentWatchMode) {
        // ---- Stopwatch ----
        case WM_STOPWATCH:
            stopwatch_running = !stopwatch_running;
            break;

        // ---- Timers ----
        case WM_TIMER:
            if (edit_field == EDIT_NONE) {
                // First press: start/stop.  If stopped & zero → enter edit mode.
                auto& t = timers[selected_timer];
                if (t.running) {
                    t.running = false;
                } else if (t.remaining_ms == 0 || t.finished) {
                    // Enter edit mode to set a duration
                    t.finished = false;
                    if (t.remaining_ms == 0) t.remaining_ms = 5 * 60 * 1000; // default 5 min
                    edit_field = EDIT_TIMER_M;   // start editing minutes
                } else {
                    t.running = true;
                    t.finished = false;
                }
            } else {
                // Cycle edit fields: H → M → S → confirm & leave edit
                if (edit_field == EDIT_TIMER_H)      edit_field = EDIT_TIMER_M;
                else if (edit_field == EDIT_TIMER_M) edit_field = EDIT_TIMER_S;
                else {
                    edit_field = EDIT_NONE;   // done editing
                }
            }
            break;

        // ---- Alarms ----
        case WM_ALARMS:
            if (edit_field == EDIT_NONE) {
                // Toggle enable, or start editing if already enabled
                auto& a = alarms[selected_alarm];
                if (!a.enabled) {
                    a.enabled = true;
                    edit_field = EDIT_ALARM_HH;
                } else {
                    // already on → edit time
                    edit_field = EDIT_ALARM_HH;
                }
            } else if (edit_field == EDIT_ALARM_HH) {
                edit_field = EDIT_ALARM_MM;
            } else {
                // finished editing this alarm
                edit_field = EDIT_NONE;
            }
            break;

        // ---- NTP ----
        case WM_NTP_SYNCH:
            if (ntp_state != NtpState::Syncing) {
                ntp_state = NtpState::Syncing;
                ntp_last_attempt_ms = (uint32_t)(esp_timer_get_time() / 1000);
                // Placeholder – real NTP would call SNTP or a custom client here.
                // For now we just fake a short delay and succeed.
                ESP_LOGI(TAG, "NTP sync requested (placeholder)");
            }
            break;

        // ---- Set time ----
        case WM_SET_TIME:
            if (edit_field == EDIT_HOUR)        edit_field = EDIT_MINUTE;
            else if (edit_field == EDIT_MINUTE) edit_field = EDIT_SECOND;
            else {
                // final field → apply
                apply_edited_time();
                edit_field = EDIT_HOUR;   // stay ready for another edit
            }
            break;

        // ---- Timezone ----
        case WM_SET_TIMEZONE:
            if (edit_field == EDIT_TZ_HOUR) {
                edit_field = EDIT_TZ_MIN;
            } else {
                // apply (offset is already live in tz_offset_*)
                edit_field = EDIT_TZ_HOUR;
                ESP_LOGI(TAG, "Timezone set to UTC%+d:%02d", tz_offset_h, tz_offset_m);
            }
            break;

        default:
            break;
    }
    on_draw();
}

void MyWatchApp::handle_back_in_mode() {
    // Cancel any in-progress edit
    edit_field = EDIT_NONE;

    if (CurrentWatchMode == WM_MAIN) {
        appManager::instance().close_current_and_open("MenuApp");
    } else {
        CurrentWatchMode = WM_MAIN;
        current_mode_index = 0;
        on_draw();
    }
}

void MyWatchApp::handle_up() {
    switch (CurrentWatchMode) {
        case WM_ALARMS:
            if (edit_field == EDIT_NONE) {
                selected_alarm = (selected_alarm + (int)alarms.size() - 1) % (int)alarms.size();
            } else if (edit_field == EDIT_ALARM_HH) {
                alarms[selected_alarm].hh = (alarms[selected_alarm].hh + 1) % 24;
            } else if (edit_field == EDIT_ALARM_MM) {
                alarms[selected_alarm].mm = (alarms[selected_alarm].mm + 1) % 60;
            }
            break;

        case WM_TIMER:
            if (edit_field == EDIT_NONE) {
                selected_timer = (selected_timer + (int)timers.size() - 1) % (int)timers.size();
            } else {
                auto& t = timers[selected_timer];
                uint32_t rem = t.remaining_ms / 1000;
                uint32_t h = rem / 3600;
                uint32_t m = (rem % 3600) / 60;
                uint32_t s = rem % 60;
                if (edit_field == EDIT_TIMER_H) h = (h + 1) % 24;
                else if (edit_field == EDIT_TIMER_M) m = (m + 1) % 60;
                else if (edit_field == EDIT_TIMER_S) s = (s + 1) % 60;
                t.remaining_ms = ((h * 3600) + (m * 60) + s) * 1000;
            }
            break;

        case WM_SET_TIME:
            if (edit_field == EDIT_HOUR)   edit_hh = (edit_hh + 1) % 24;
            else if (edit_field == EDIT_MINUTE) edit_mm = (edit_mm + 1) % 60;
            else if (edit_field == EDIT_SECOND) edit_ss = (edit_ss + 1) % 60;
            break;

        case WM_SET_TIMEZONE:
            if (edit_field == EDIT_TZ_HOUR) {
                tz_offset_h++;
                if (tz_offset_h > 14) tz_offset_h = -12;
            } else if (edit_field == EDIT_TZ_MIN) {
                tz_offset_m = (tz_offset_m == 0) ? 30 : 0;
            }
            break;

        default:
            break;
    }
    on_draw();
}

void MyWatchApp::handle_down() {
    switch (CurrentWatchMode) {
        case WM_ALARMS:
            if (edit_field == EDIT_NONE) {
                selected_alarm = (selected_alarm + 1) % (int)alarms.size();
            } else if (edit_field == EDIT_ALARM_HH) {
                alarms[selected_alarm].hh = (alarms[selected_alarm].hh + 23) % 24;
            } else if (edit_field == EDIT_ALARM_MM) {
                alarms[selected_alarm].mm = (alarms[selected_alarm].mm + 59) % 60;
            }
            break;

        case WM_TIMER:
            if (edit_field == EDIT_NONE) {
                selected_timer = (selected_timer + 1) % (int)timers.size();
            } else {
                auto& t = timers[selected_timer];
                uint32_t rem = t.remaining_ms / 1000;
                uint32_t h = rem / 3600;
                uint32_t m = (rem % 3600) / 60;
                uint32_t s = rem % 60;
                if (edit_field == EDIT_TIMER_H) h = (h + 23) % 24;
                else if (edit_field == EDIT_TIMER_M) m = (m + 59) % 60;
                else if (edit_field == EDIT_TIMER_S) s = (s + 59) % 60;
                t.remaining_ms = ((h * 3600) + (m * 60) + s) * 1000;
            }
            break;

        case WM_SET_TIME:
            if (edit_field == EDIT_HOUR)   edit_hh = (edit_hh + 23) % 24;
            else if (edit_field == EDIT_MINUTE) edit_mm = (edit_mm + 59) % 60;
            else if (edit_field == EDIT_SECOND) edit_ss = (edit_ss + 59) % 60;
            break;

        case WM_SET_TIMEZONE:
            if (edit_field == EDIT_TZ_HOUR) {
                tz_offset_h--;
                if (tz_offset_h < -12) tz_offset_h = 14;
            } else if (edit_field == EDIT_TZ_MIN) {
                tz_offset_m = (tz_offset_m == 0) ? 30 : 0;
            }
            break;

        default:
            break;
    }
    on_draw();
}

// ---------------------------------------------------------------------------
// Input entry point
// ---------------------------------------------------------------------------
void MyWatchApp::receive_event_input(const void* event) {
    if (!event) return;

    const InputEvent* ev = static_cast<const InputEvent*>(event);

    // Hold-ENTER special cases
    if (ev->action == KeyAction::Hold && ev->key == KEY_ENTER) {
        switch (CurrentWatchMode) {
            case WM_STOPWATCH:
                // Reset stopwatch
                stopwatch_running = false;
                stopwatch_ms = 0;
                on_draw();
                return;

            case WM_SET_TIME:
                apply_edited_time();
                edit_field = EDIT_HOUR;
                on_draw();
                return;

            case WM_SET_TIMEZONE:
                ESP_LOGI(TAG, "Timezone confirmed UTC%+d:%02d", tz_offset_h, tz_offset_m);
                edit_field = EDIT_TZ_HOUR;
                on_draw();
                return;

            case WM_ALARMS:
                // Long-press disables the selected alarm
                alarms[selected_alarm].enabled = false;
                edit_field = EDIT_NONE;
                on_draw();
                return;

            case WM_TIMER:
                // Long-press clears the selected timer
                timers[selected_timer].remaining_ms = 0;
                timers[selected_timer].running = false;
                timers[selected_timer].finished = false;
                edit_field = EDIT_NONE;
                on_draw();
                return;

            default:
                break;
        }
    }

    if (ev->action != KeyAction::Tap) return;

    switch (ev->key) {
        case KEY_LEFT:  prev_mode();   break;
        case KEY_RIGHT: next_mode();   break;
        case KEY_ENTER: handle_enter(); break;
        case KEY_BACK:  handle_back_in_mode(); break;
        case KEY_UP:    handle_up();   break;
        case KEY_DOWN:  handle_down(); break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
static std::shared_ptr<AppBase> create_watch(const ApplicationConfig& cfg) {
    return std::make_shared<MyWatchApp>(cfg);
}

void register_watch() {
    AppManifest m;
    m.name = "WatchApp";
    m.display_name = "Watch";
    m.description = "Main watch face + stopwatch / timer / alarms / set time / ULP sync";
    m.capabilities = static_cast<uint32_t>(AppCapability::FULLSCREEN) |
                     static_cast<uint32_t>(AppCapability::NEEDS_WINDOW);
    m.stack_size_bytes = 8192;
    m.priority = 5;
    m.tick_rate_hz = 20;
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<MyWatchApp>(cfg);
    };

    appManager::instance().register_app(m);
}
