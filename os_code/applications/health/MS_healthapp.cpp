#include "MS_healthapp.hpp"
#include "hardware/drivers/heartMonitor/hr_metrics.h"
#include "hardware/drivers/heartMonitor/hr_profile.h"
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
    CanvasCfg cfg;
    cfg.x = 8; cfg.y = 120; cfg.width = 224; cfg.height = 90;
    cfg.bgColor = 0x0000;
    cfg.parentWindow = win.get();
    canvas = win->AddCanvas(cfg);
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

    hr_profile_t prof;
    hr_profile_get(&prof);
    const char *band = "band: --";
    if (prof.n >= 8) {
        int b = hr_profile_bpm_band(bpm);
        band = (b < 0) ? "band: low" : (b > 0) ? "band: high" : "band: normal";
    }

    char screen[360];
    snprintf(screen, sizeof screen,
             "HEALTH  overview\n"
             "\n"
             "  %3u bpm\n"
             "  SpO2 %u%%\n"
             "  %s\n"
             "  %s  (%u-%u)\n"
             "  HRV %u ms%s\n"
             "\n"
             "  %s\n"
             "log %u B   ENTER live\n",
             bpm, spo2,
             motion ? "motion: reject" : "motion: clear",
             band, prof.bpm_lo, prof.bpm_hi,
             prof.hrv_rmssd_ms, prof.hrv_valid ? "" : " ?",
             spark, hr_metrics_used());
    win->SetText(screen);
    win->dirty = true;
}

void HealthApp::draw_bars(const uint8_t *bpm, const uint8_t *flags, int n,
                         uint8_t mn, uint8_t mx)
{
    if (!canvas) return;
    canvas->ClearShapes();
    if (n <= 0) return;
    int span = (int)mx - (int)mn;
    if (span < 8) span = 8;
    int bw = 224 / (n > 0 ? n : 1);
    if (bw < 2) bw = 2;
    if (bw > 8) bw = 8;
    for (int i = 0; i < n; i++) {
        int h = ((int)bpm[i] - (int)mn) * 80 / span;
        if (h < 2) h = 2;
        if (h > 86) h = 86;
        s_bounds_16u b;
        b.x = (uint16_t)(i * (224 / n));
        b.y = (uint16_t)(86 - h);
        b.w = (uint16_t)(bw - 1);
        b.h = (uint16_t)h;
        uint16_t col = 0x07E0;                 // green
        if (flags && (flags[i] & 1)) col = 0xFD20;  // orange if motion
        else if (bpm[i] > 140) col = 0xF800;   // red if high
        canvas->AddShape(SHAPE_RECT, b, col, 1);
    }
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

    uint8_t bpms[40], flags[40];
    for (int i = 0; i < n; i++) { bpms[i] = s[i].bpm; flags[i] = s[i].flags; }
    draw_bars(bpms, flags, n, mn, mx);

    char screen[280];
    snprintf(screen, sizeof screen,
             "HEALTH  live:%s\n"
             "\n"
             "  %3u bpm   avg %u\n"
             "  SpO2 %u%%\n"
             "  min %u  max %u   n %d\n"
             "\n"
             "\n\n\n\n\n"
             "ENTER stop   BACK menu\n"
             "saves .rle\n",
             hr_live_state_name(hr_live_state()),
             bpm, avg, spo2, mn, mx, hr_live_count());
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
            if (canvas) canvas->ClearShapes();
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
