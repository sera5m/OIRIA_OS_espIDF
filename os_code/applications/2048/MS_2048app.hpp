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
// 2048 – welded text-on-tile squares via AnimWorld
// ---------------------------------------------------------------------------
// Controls:
//   UP / DOWN / LEFT / RIGHT – slide board
//   HOLD-ENTER               – new game
//   BACK                     – menu
// ---------------------------------------------------------------------------

class Game2048App : public AppBase {
public:
    explicit Game2048App(const ApplicationConfig& cfg);

    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void on_draw() override;

    void on_start() override;
    void on_stop() override;
    void on_pause() override;
    void on_resume() override;

private:
    static constexpr int N = 4;

    std::shared_ptr<Window>    win;
    std::shared_ptr<Canvas>    canvas;
    std::unique_ptr<AnimWorld> world;

    // Board values (0 = empty). Visual tiles created/destroyed on change.
    int      board[N][N] = {{0}};
    uint16_t tile_ids[N][N] = {{0}};    // value tiles (welded text)
    uint16_t empty_ids[N][N] = {{0}};   // dim background cells

    float cell = 48.f;
    float gap  = 4.f;
    float field_w = 0.f;
    float field_h = 0.f;

    int  score = 0;
    bool won   = false;
    bool lost  = false;
    bool scored_this_round = false;

    GameStats stats{};
    static constexpr const char* kStatsName = "Game2048App";

    void build_world();
    void reset_game();
    void spawn_random();
    bool slide(int dx, int dy);     // returns true if anything moved
    bool can_move() const;
    void rebuild_visuals();
    void update_hud();
    AwVec2 cell_center(int c, int r) const;
    uint16_t color_for(int value) const;
};

void register_2048();
