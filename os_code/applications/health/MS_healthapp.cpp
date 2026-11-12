#include "MS_healthapp.hpp"
#include "hardware/drivers/heartMonitor/hr_metrics.h"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "os_code/middle_layer/input/hid_t.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "health_app";

HealthApp::HealthApp(const ApplicationConfig& cfg) : AppBase(cfg) {}

void HealthApp::on_start() {
    win = std::make_shared<Window>("Health", 0, 0, 240, 240);
    WindowManager::getInstance().registerWindow(win);
    mode = HealthMode::Overview;
    redraw();
}

void HealthApp::on_stop() {
    if (hr_live_state() == HR_LIVE_RUNNING || hr_live_state() == HR_LIVE_PAUSED)
        hr_live_stop();
    if (win) {
        WindowManager::getInstance().unregisterWindow(win);
        win.reset();
    }
}

void HealthApp::redraw_overview() {
    uint8_t recent[40];
    uint8_t spo2 = 0;
    bool motion = false;
    int n = hr_metrics_recent(recent, 40, &spo2, &motion);
    uint8_t bpm = (n > 0) ? recent[n - 1] : 0;

    char spark[42];
    const char *bars = " .:-=+*#";
    memset(spark, ' ', 40);
    spark[40] = 0;
    if (n > 0) {
        uint8_t mn = recent[0], mx = recent[0];
        for (int i = 1; i < n; i++) {
            if (recent[i] < mn) mn = recent[i];
            if (recent[i] > mx) mx = recent[i];
        }
        int span = (int)mx - (int)mn; if (span < 1) span = 1;
        for (int i = 0; i < n && i < 40; i++) {
            int h = ((int)recent[i] - (int)mn) * 7 / span;
            if (h < 0) h = 0; if (h > 7) h = 7;
            spark[i] = bars[h];
        }
    }

    char screen[320];
    snprintf(screen, sizeof screen,
             "HEALTH  overview\n"
             "\n"
             "  %3u bpm\n"
             "  SpO2 %u%%\n"
             "  %s\n"
             "\n"
             "  %s\n"
             "\n"
             "log %u B\n"
             "ENTER live   BACK menu\n",
             bpm, spo2,
             motion ? "motion: reject" : "motion: clear",
             spark, hr_metrics_used());
    win->SetText(screen);
    win->dirty = true;
}

void HealthApp::redraw_live() {
    hr_live_sample_t s[40];
    int n = hr_live_copy(s, 40);
    uint8_t mn = 0, mx = 0, avg = 0;
    hr_live_stats(&mn, &mx, &avg);
    uint8_t bpm = (n > 0) ? s[n - 1].bpm : 0;
    uint8_t spo2 = (n > 0) ? s[n - 1].spo2 : 0;

    char spark[42];
    const char *bars = " .:-=+*#";
    memset(spark, ' ', 40);
    spark[40] = 0;
    if (n > 0) {
        int span = (int)mx - (int)mn; if (span < 1) span = 1;
        for (int i = 0; i < n; i++) {
            int h = ((int)s[i].bpm - (int)mn) * 7 / span;
            if (h < 0) h = 0; if (h > 7) h = 7;
            spark[i] = bars[h];
        }
    }

    char screen[320];
    snprintf(screen, sizeof screen,
             "HEALTH  live:%s\n"
             "\n"
             "  %3u bpm   avg %u\n"
             "  SpO2 %u%%\n"
             "  min %u  max %u\n"
             "  samples %d\n"
             "\n"
             "  %s\n"
             "\n"
             "ENTER stop   BACK menu\n",
             hr_live_state_name(hr_live_state()),
             bpm, avg, spo2, mn, mx, hr_live_count(),
             spark);
    win->SetText(screen);
    win->dirty = true;
}

void HealthApp::redraw() {
    if (!win) return;
    if (mode == HealthMode::Live) redraw_live();
    else redraw_overview();
}

void HealthApp::tick_app(uint32_t dt) {
    acc_ms += dt;
    if (acc_ms < 500) return;
    acc_ms = 0;
    redraw();
}

void HealthApp::receive_event_input(const void* event) {
    if (!event) return;
    const InputEvent* ev = static_cast<const InputEvent*>(event);
    if (ev->action != KeyAction::Tap) return;

    if (ev->key == KEY_BACK) {
        if (mode == HealthMode::Live) hr_live_stop();
        appManager::instance().close_current_and_open("MenuApp");
        return;
    }
    if (ev->key == KEY_ENTER) {
        if (mode == HealthMode::Overview) {
            hr_live_start();
            mode = HealthMode::Live;
        } else {
            hr_live_stop();
            mode = HealthMode::Overview;
        }
        redraw();
    }
}

void register_health() {
    AppManifest m;
    m.name = "HealthApp";
    m.display_name = "Health";
    m.description = "Heart rate, SpO2, live session";
    m.capabilities = static_cast<uint32_t>(AppCapability::NEEDS_WINDOW);
    m.stack_size_bytes = 8192;
    m.priority = 5;
    m.tick_rate_hz = 4;
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<HealthApp>(cfg);
    };
    appManager::instance().register_app(m);
    ESP_LOGI(TAG, "health app registered");
}
