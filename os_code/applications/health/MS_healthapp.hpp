#pragma once
// Simple health viewer: current BPM, SpO2, motion gate, and a sparkline
// of the recent reduced samples from hr_metrics.

#include <stdint.h>
#include <memory>
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/window_env/MWenv.hpp"

class HealthApp : public AppBase {
public:
    explicit HealthApp(const ApplicationConfig& cfg);
    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void on_start() override;
    void on_stop() override;

private:
    std::shared_ptr<Window> win;
    uint32_t acc_ms = 0;
    void redraw();
};

void register_health();
