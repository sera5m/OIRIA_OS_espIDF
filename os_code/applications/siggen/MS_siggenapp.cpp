#include "MS_siggenapp.hpp"

#include <stdio.h>
#include <string.h>

#include "code_stuff/signal_gen/siggen_play.h"
#include "code_stuff/signal_gen/siggen_wavetable.h"

static const char* TAG = "SigGenApp";
static const char* kWaves[] = {"sine", "square", "tri", "saw", "noise"};

SigGenApp::SigGenApp(const ApplicationConfig& cfg) : AppBase(cfg) {
    appTickRateHZ = 8;
}

void SigGenApp::on_start() {
    ESP_LOGI(TAG, "start");
    pin = SIGGEN_DEFAULT_OUT_GPIO;
    pull_cfg();

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
            .BorderColor = 0x07E0,
            .BgColor = 0x0000,
            .Bg_secondaryColor = 0x0000,
            .WinTextColor = 0x07E0,
            .backgroundType = BgFillType::Solid,
            .UpdateRate = 0.2f
        },
        "SigGen"
    );
    strncpy(win->Initialcfg.name, "SigGen", sizeof win->Initialcfg.name - 1);
    WindowManager::getInstance().registerWindow(win);
    bind_main_window(win);

    CanvasCfg cc;
    cc.x = 8; cc.y = 108;
    cc.width = 264; cc.height = 88;
    cc.borderless = true;
    cc.DrawBG = true;
    cc.bgColor = 0x0841;
    cc.parentWindow = win.get();
    canvas = win->AddCanvas(cc);
    if (canvas) plot.init(canvas.get(), cc.width, cc.height, 48, 0x07E0);

    draw_preview();
    redraw();
}

void SigGenApp::on_stop() {
    if (win) {
        WindowManager::getInstance().unregisterWindow(win);
        win.reset();
    }
    canvas.reset();
}

void SigGenApp::on_pause() {}
void SigGenApp::on_resume() { pull_cfg(); redraw(); }

void SigGenApp::pull_cfg() {
    const siggen_cfg_t* c = siggen_play_cfg();
    if (!c) return;
    running = c->running;
    wave = (int)c->wave;
    if (c->freq_hz) hz = (int)c->freq_hz;
    duty = c->duty_percent;
    amp  = c->amplitude;
    int g = siggen_play_gpio();
    if (g > 0) pin = g;
}

void SigGenApp::apply(bool start) {
    if (start) {
        siggen_play(wave, (uint32_t)hz, (uint8_t)duty, pin, (uint8_t)amp);
        running = true;
    } else {
        siggen_play_stop();
        running = false;
    }
}

void SigGenApp::nudge(int dir) {
    switch (field) {
    case F_WAVE:
        wave += dir;
        if (wave < 0) wave = 4;
        if (wave > 4) wave = 0;
        break;
    case F_HZ: {
        int step = hz < 200 ? 10 : (hz < 2000 ? 100 : 500);
        hz += dir * step;
        if (hz < 1) hz = 1;
        if (hz > 20000) hz = 20000;
        break;
    }
    case F_DUTY:
        duty += dir * 5;
        if (duty < 5) duty = 5;
        if (duty > 95) duty = 95;
        break;
    case F_AMP:
        amp += dir * 5;
        if (amp < 5) amp = 5;
        if (amp > 100) amp = 100;
        break;
    case F_PIN:
        pin += dir;
        if (pin < 1) pin = 1;
        if (pin > 21) pin = 21;
        break;
    default: break;
    }
    if (running) apply(true);
    else draw_preview();
}

void SigGenApp::draw_preview() {
    int16_t buf[48];
    uint32_t ph = 0;
    uint32_t inc = 0xFFFFFFFFu / 48u;
    int16_t a = (int16_t)((amp * 30000) / 100);
    siggen_fill((siggen_wave_t)wave, buf, 48, &ph, inc, a, (uint8_t)duty);
    plot.set_q15(buf, 48);
    if (win) win->dirty = true;
}

void SigGenApp::redraw() {
    if (!win) return;
    static const char* flab[] = {"WAVE", "HZ", "DUTY", "AMP", "PIN"};
    char screen[480];
    const char* wn = (wave >= 0 && wave <= 4) ? kWaves[wave] : "?";
    snprintf(screen, sizeof screen,
             "Signal gen  %s\n"
             "  %c wave  %s\n"
             "  %c hz    %d\n"
             "  %c duty  %d%%\n"
             "  %c amp   %d%%\n"
             "  %c pin   GPIO %d\n"
             "\n"
             "ENTER start/stop\n"
             "L/R field  U/D value\n"
             "web: /  tab Wave\n",
             running ? "RUN" : "stop",
             field == F_WAVE ? '>' : ' ', wn,
             field == F_HZ   ? '>' : ' ', hz,
             field == F_DUTY ? '>' : ' ', duty,
             field == F_AMP  ? '>' : ' ', amp,
             field == F_PIN  ? '>' : ' ', pin);
    (void)flab;
    win->SetText(screen);
    win->dirty = true;
}

void SigGenApp::tick_app(uint32_t) {
    pull_cfg();
    draw_preview();
    redraw();
}

void SigGenApp::on_draw() {
    if (win) win->dirty = true;
}

void SigGenApp::receive_event_input(const void* event) {
    if (!event) return;
    const InputEvent* ev = static_cast<const InputEvent*>(event);
    if (ev->action != KeyAction::Tap) return;
    switch (ev->key) {
    case KEY_BACK:
        appManager::instance().close_current_and_open("MenuApp");
        break;
    case KEY_ENTER:
        apply(!running);
        draw_preview();
        redraw();
        break;
    case KEY_LEFT:
        field = (field + F_COUNT - 1) % F_COUNT;
        redraw();
        break;
    case KEY_RIGHT:
        field = (field + 1) % F_COUNT;
        redraw();
        break;
    case KEY_UP:
        nudge(+1);
        redraw();
        break;
    case KEY_DOWN:
        nudge(-1);
        redraw();
        break;
    default:
        break;
    }
}

void register_siggen() {
    AppManifest m;
    m.name = "SigGenApp";
    m.display_name = "SigGen";
    m.description = "PWM function generator (web + watch)";
    m.capabilities = static_cast<uint32_t>(AppCapability::NEEDS_WINDOW) |
                     static_cast<uint32_t>(AppCapability::RAW_GPIO_ACCESS);
    m.stack_size_bytes = 12288;
    m.priority = 5;
    m.tick_rate_hz = 8;
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<SigGenApp>(cfg);
    };
    appManager::instance().register_app(m);
}
