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
// Full InputEvent / KeyAction (same stack as WatchApp).
// Do not rely only on the opaque forward typedef in rshell_streamdefs.h.
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"
#include "os_code/applications/game_stats.hpp"

// ---------------------------------------------------------------------------
// Pong – AnimWorld + Canvas mini-game
// ---------------------------------------------------------------------------
// Controls:
//   UP / DOWN  – move player paddle (right side)
//   ENTER      – start / pause
//   BACK       – return to menu
//   HOLD-ENTER – reset score + serve
//
// AI (left paddle): clamped vertical velocity; aims at the ball after
// simulating up to 2 top/bottom wall rebounds. Predicted path is drawn
// as dim debug segments when show_prediction is true.
// ---------------------------------------------------------------------------

class PongApp : public AppBase {
public:
    explicit PongApp(const ApplicationConfig& cfg);

    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void on_draw() override;

    void on_start() override;
    void on_stop() override;
    void on_pause() override;
    void on_resume() override;

private:
    std::shared_ptr<Window>  pong_window;
    std::shared_ptr<Canvas>  pong_canvas;
    std::unique_ptr<AnimWorld> world;

    // Object ids
    uint16_t ball_id   = 0;
    uint16_t player_id = 0;   // right paddle
    uint16_t ai_id     = 0;   // left paddle

    // Playfield (world units ≈ pixels at scale 1)
    float field_w = 240.f;
    float field_h = 200.f;
    float paddle_hw = 4.f;
    float paddle_hh = 22.f;
    float ball_r    = 5.f;

    float player_speed = 160.f;   // px/s while holding up/down
    float ai_max_speed = 110.f;   // clamped AI vertical speed
    float ball_speed   = 95.f;    // serve speed

    int  score_player = 0;
    int  score_ai     = 0;
    bool running      = false;    // ball in play
    bool paused       = false;

    GameStats stats{};            // high score persistence (/sdcard/apps/savedat/)
    static constexpr const char* kStatsName = "PongApp";

    // Held keys for continuous paddle motion
    bool hold_up   = false;
    bool hold_down = false;

    // AI prediction polyline (world space) – up to 3 segments (2 rebounds)
    static constexpr int PREDICT_MAX_PTS = 4;
    AwVec2 predict_pts[PREDICT_MAX_PTS];
    int    predict_count = 0;
    bool   show_prediction = true;

    // --- setup ---
    void build_world();
    void serve_ball(bool toward_player);
    void reset_round();

    // --- sim helpers ---
    void update_player(float dt);
    void update_ai(float dt);
    void predict_ball_path();           // fills predict_pts
    void check_goals();
    void draw_debug_prediction();       // fb_line segments after world draw

    void update_score_text();
};

void register_pong();
