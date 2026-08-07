#pragma once

#include <stdint.h>
#include <memory>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/core/window_env/Canvas.hpp"
#include "os_code/core/window_env/AnimWorld.hpp"
#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"
#include "os_code/applications/game_stats.hpp"

// ---------------------------------------------------------------------------
// Snake – grid game on AnimWorld + Canvas
// ---------------------------------------------------------------------------
// Controls:
//   UP / DOWN / LEFT / RIGHT – change direction (no 180° reverse)
//   ENTER                   – pause / unpause
//   HOLD-ENTER              – restart
//   BACK                    – menu
// ---------------------------------------------------------------------------

class SnakeApp : public AppBase {
public:
    explicit SnakeApp(const ApplicationConfig& cfg);

    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void on_draw() override;

    void on_start() override;
    void on_stop() override;
    void on_pause() override;
    void on_resume() override;

private:
    static constexpr int GRID_W = 16;
    static constexpr int GRID_H = 12;
    static constexpr int MAX_LEN = GRID_W * GRID_H;

    std::shared_ptr<Window>    win;
    std::shared_ptr<Canvas>    canvas;
    std::unique_ptr<AnimWorld> world;

    // One AnimWorld object per segment + one for food
    uint16_t seg_ids[MAX_LEN] = {0};
    uint16_t food_id = 0;
    int      length  = 0;

    // Grid positions (head = index 0)
    int8_t gx[MAX_LEN] = {0};
    int8_t gy[MAX_LEN] = {0};

    int8_t dir_x = 1, dir_y = 0;
    int8_t pending_dx = 1, pending_dy = 0;
    int8_t food_x = 0, food_y = 0;

    float cell = 14.f;          // world pixels per cell
    float field_w = 0.f;
    float field_h = 0.f;

    int      score = 0;
    bool     running = false;
    bool     paused  = false;
    bool     game_over = false;
    bool     scored_this_round = false;  // avoid double-record on game over

    GameStats stats{};
    static constexpr const char* kStatsName = "SnakeApp";

    uint32_t step_ms = 180;     // move period (speeds up slightly with score)
    uint32_t accum_ms = 0;

    void build_world();
    void reset_game();
    void spawn_food();
    void step_snake();
    void sync_sprites();
    void update_hud();
    AwVec2 cell_to_world(int cx, int cy) const;
};

void register_snake();
