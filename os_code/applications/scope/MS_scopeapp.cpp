#include "MS_scopeapp.hpp"

#include <stdio.h>
#include <string.h>

#include "code_stuff/signal_gen/siggen_play.h"

static const char* TAG = "ScopeApp";

ScopeApp::ScopeApp(const ApplicationConfig& cfg) : AppBase(cfg) {
    appTickRateHZ = 8;
}

void ScopeApp::on_start() {
    ESP_LOGI(TAG, "start");
    pin = siggen_scope_gpio();
    if (pin <= 0) pin = SIGGEN_DEFAULT_SCOPE_GPIO;
    live = true;

    win = std::make_shared<Window>(
        WindowCfg{
            .Posx = 0, .Posy = 0,
            .Layer = 0, .renderPriority = 0,
            .win_width  = 280,
            .win_height = 240,
            .win_rotation = 1,
            .AutoAlignment = false,
            .WrapText = true,
            .borderless = false,
            .ShowNameAtTopOfWindow = true,
            .TextSizeMult = 1,
            .BorderColor = 0x07FF,
            .BgColor = 0x0000,
            .Bg_secondaryColor = 0x0000,
            .WinTextColor = 0x07E0,
            .backgroundType = BgFillType::Solid,
            .UpdateRate = 0.15f
        },
        "Scope"
    );
    strncpy(win->Initialcfg.name, "Scope", sizeof win->Initialcfg.name - 1);
    WindowManager::getInstance().registerWindow(win);
    bind_main_window(win);

    CanvasCfg cc;
    cc.x = 8; cc.y = 88;
    cc.width = 264; cc.height = 112;
    cc.borderless = true;
    cc.DrawBG = true;
    cc.bgColor = 0x0841;
    cc.parentWindow = win.get();
    canvas = win->AddCanvas(cc);
    if (canvas) plot.init(canvas.get(), cc.width, cc.height, N, 0x07FF);

    capture();
    redraw();
}

void ScopeApp::on_stop() {
    if (win) {
        WindowManager::getInstance().unregisterWindow(win);
        win.reset();
    }
    canvas.reset();
}

void ScopeApp::on_pause() { live = false; }
void ScopeApp::on_resume() {
    live = true;
    int g = siggen_scope_gpio();
    if (g > 0) pin = g;
    redraw();
}

void ScopeApp::capture() {
    got = siggen_scope_cap(pin, N, samples, NULL);
    if (got > 0) last_mv = samples[got - 1];
}

void ScopeApp::redraw() {
    if (!win) return;
    int mn = 0, mx = 1;
    if (got > 0) {
        mn = mx = samples[0];
        for (int i = 1; i < got; i++) {
            if (samples[i] < mn) mn = samples[i];
            if (samples[i] > mx) mx = samples[i];
        }
        plot.set_mv(samples, got, mn, mx);
    }
    char screen[360];
    snprintf(screen, sizeof screen,
             "Scope  %s\n"
             "pin GPIO %d  (U/D)\n"
             "last %d mV\n"
             "min %d  max %d\n"
             "ENTER freeze   BACK menu\n"
             "web: /  tab Scope\n",
             live ? "LIVE" : "HOLD",
             pin, last_mv, mn, mx);
    win->SetText(screen);
    win->dirty = true;
}

void ScopeApp::tick_app(uint32_t dt) {
    acc_ms += dt;
    if (!live) return;
    int g = siggen_scope_gpio();
    if (g > 0) pin = g;
    if (acc_ms < 120) return;
    acc_ms = 0;
    capture();
    redraw();
}

void ScopeApp::on_draw() {
    if (win) win->dirty = true;
}

void ScopeApp::receive_event_input(const void* event) {
    if (!event) return;
    const InputEvent* ev = static_cast<const InputEvent*>(event);
    if (ev->action != KeyAction::Tap) return;
    switch (ev->key) {
    case KEY_BACK:
        appManager::instance().close_current_and_open("MenuApp");
        break;
    case KEY_ENTER:
        live = !live;
        if (live) capture();
        redraw();
        break;
    case KEY_UP:
        pin++;
        if (pin > 21) pin = 1;
        if (live) capture();
        redraw();
        break;
    case KEY_DOWN:
        pin--;
        if (pin < 1) pin = 21;
        if (live) capture();
        redraw();
        break;
    default:
        break;
    }
}

void register_scope() {
    AppManifest m;
    m.name = "ScopeApp";
    m.display_name = "Scope";
    m.description = "ADC oscilloscope (web + watch)";
    m.capabilities = static_cast<uint32_t>(AppCapability::NEEDS_WINDOW) |
                     static_cast<uint32_t>(AppCapability::RAW_GPIO_ACCESS);
    m.stack_size_bytes = 12288;
    m.priority = 5;
    m.tick_rate_hz = 8;
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<ScopeApp>(cfg);
    };
    appManager::instance().register_app(m);
}
