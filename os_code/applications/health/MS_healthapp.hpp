#pragma once
// Health viewer.
// Overview: passive log (BPM, SpO2, motion, sparkline).
// Live: dense session driven by hr_live state machine.
// ENTER: overview/graph → live, live → overview.
// UP: overview ↔ graph. BACK leaves (stops a running session).

#include <stdint.h>
#include <memory>
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/core/window_env/Canvas.hpp"
#include "hardware/drivers/heartMonitor/hr_live.h"
#include "hardware/drivers/lcd/st7789v2/t_shapes.h"

enum class HealthMode : uint8_t {
    Overview = 0,
    Graph,
    Live,
};

class HealthApp : public AppBase {
public:
    explicit HealthApp(const ApplicationConfig& cfg);
    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void on_start() override;
    void on_stop() override;

private:
    std::shared_ptr<Window> win;
    std::shared_ptr<Canvas> canvas;
    HealthMode mode = HealthMode::Overview;
    uint32_t acc_ms = 0;

    void draw_bars(const uint8_t *bpm, const uint8_t *flags, int n,
                   uint8_t mn, uint8_t mx);

    void redraw_overview();
    void redraw_graph();
    void redraw_live();
    void redraw();
};

void register_health();
