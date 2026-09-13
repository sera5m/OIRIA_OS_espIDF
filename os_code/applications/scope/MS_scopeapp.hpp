#pragma once
// Oscilloscope on the watch. Same ADC capture as /scope.json on the HTML console.
// ENTER freeze/run, UP/DOWN pin, BACK menu.

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

class ScopeApp : public AppBase {
public:
    explicit ScopeApp(const ApplicationConfig& cfg);

    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void on_draw() override;
    void on_start() override;
    void on_stop() override;
    void on_pause() override;
    void on_resume() override;

private:
    static constexpr int N = 48;

    std::shared_ptr<Window> win;
    std::shared_ptr<Canvas> canvas;
    TracePlot plot;

    int pin = 1;
    bool live = true;
    int16_t samples[N]{};
    int got = 0;
    int last_mv = 0;
    uint32_t acc_ms = 0;

    void capture();
    void redraw();
};

void register_scope();
