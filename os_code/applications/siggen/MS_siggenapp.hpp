#pragma once
// Function generator on the watch. Same siggen_play backend as the HTML console.
// Encoder: LEFT/RIGHT field, UP/DOWN value, ENTER start/stop, BACK menu.

#include <stdint.h>
#include <memory>

#include "esp_log.h"
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/core/window_env/Canvas.hpp"
#include "os_code/applications/siglab/trace_plot.hpp"
#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/middle_layer/input/input_handler.hpp"

class SigGenApp : public AppBase {
public:
    explicit SigGenApp(const ApplicationConfig& cfg);

    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void on_draw() override;
    void on_start() override;
    void on_stop() override;
    void on_pause() override;
    void on_resume() override;

private:
    enum Field : int { F_WAVE = 0, F_HZ, F_DUTY, F_AMP, F_PIN, F_COUNT };

    std::shared_ptr<Window> win;
    std::shared_ptr<Canvas> canvas;
    TracePlot plot;

    int wave = 1;      // square default
    int hz   = 1000;
    int duty = 50;
    int amp  = 80;
    int pin  = 4;
    int field = F_HZ;
    bool running = false;

    void pull_cfg();
    void apply(bool start);
    void nudge(int dir);
    void redraw();
    void draw_preview();
};

void register_siggen();
