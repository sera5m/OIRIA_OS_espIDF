#include "MS_pongapp.hpp"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

#include "code_stuff/helperfunctions.hpp"
#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"

static const char* TAG = "PongApp";

// RGB565 helpers
static constexpr uint16_t COL_BG     = 0x1082;  // dark
static constexpr uint16_t COL_BORDER = 0xC618;
static constexpr uint16_t COL_PLAYER = 0x07E0;  // green
static constexpr uint16_t COL_AI     = 0xF800;  // red
static constexpr uint16_t COL_BALL   = 0xFFFF;
static constexpr uint16_t COL_PRED   = 0x8410;  // grey debug path
static constexpr uint16_t COL_TEXT   = 0xFFFF;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

PongApp::PongApp(const ApplicationConfig& cfg) : AppBase(cfg) {
    appTickRateHZ = 30;   // smooth enough for paddle + ball
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void PongApp::on_start() {
    ESP_LOGI(TAG, "Pong starting");

    pong_window = std::make_shared<Window>(
        WindowCfg{
            .Posx = 0, .Posy = 0,
            .Layer = 0, .renderPriority = 0,
            .win_width  = 280,
            .win_height = 240,
            .win_rotation = 1,
            .AutoAlignment = false,
            .WrapText = true,
            .borderless = true,
            .ShowNameAtTopOfWindow = false,
            .TextSizeMult = 1,
            .BorderColor = COL_BORDER,
            .BgColor = COL_BG,
            .Bg_secondaryColor = 0x2104,
            .WinTextColor = COL_TEXT,
            .backgroundType = BgFillType::Solid,
            .UpdateRate = 0.5f
        },
        "Pong"
    );

    WindowManager::getInstance().registerWindow(pong_window);
    bind_main_window(pong_window);

    // Canvas covers most of the window; leave a strip for score text
    CanvasCfg cc;
    cc.x = 0;
    cc.y = 20;
    cc.width  = (int)field_w;
    cc.height = (int)field_h;
    cc.borderless = true;
    cc.DrawBG = false;
    cc.parentWindow = pong_window.get();
    pong_canvas = pong_window->AddCanvas(cc);

    build_world();

    if (pong_canvas && world) {
        pong_canvas->AttachWorld(world.get());
        AwCamera cam;
        cam.origin   = {0.f, 0.f};
        cam.scale    = 1.f;
        cam.offset_x = cc.x;
        cam.offset_y = cc.y;
        pong_canvas->SetWorldCamera(cam);
        // We draw prediction ourselves after world draw
        pong_canvas->SetAutoDrawWorld(true);
    }

    score_player = 0;
    score_ai = 0;
    running = false;
    paused = false;
    game_stats::load(kStatsName, stats);
    serve_ball(true);
    update_score_text();
    on_draw();
}

void PongApp::on_stop() {
    if (score_player > 0 || score_ai > 0) {
        game_stats::record_round(kStatsName, stats, (uint32_t)score_player);
    }
    if (pong_canvas) {
        pong_canvas->DetachWorld();
        pong_canvas.reset();
    }
    world.reset();
    if (pong_window) {
        WindowManager::getInstance().unregisterWindow(pong_window);
        pong_window.reset();
    }
    ESP_LOGI(TAG, "Pong stopped");
}

void PongApp::on_pause() {
    paused = true;
    ESP_LOGI(TAG, "Pong paused");
}

void PongApp::on_resume() {
    paused = false;
    ESP_LOGI(TAG, "Pong resumed");
}

// ---------------------------------------------------------------------------
// World construction
// ---------------------------------------------------------------------------

void PongApp::build_world() {
    AwWorldConfig cfg;
    cfg.dim = AwWorldDim::Dim2D;
    cfg.gravity = {0.f, 0.f};
    cfg.fixed_dt = 1.f / 60.f;
    cfg.velocity_iterations = 2;
    cfg.enable_object_collisions = true;
    cfg.enable_border_collisions = true;

    // No engine border: top/bottom bounce is done in tick; left/right are goals.
    cfg.border.type = AwBorderType::None;
    cfg.enable_border_collisions = false;

    world = std::make_unique<AnimWorld>(cfg);

    // --- paddles (kinematic) ---
    AwShape paddle_shape = AwShape::make_rect(paddle_hw, paddle_hh);

    player_id = world->create_object("player",
        {field_w - 14.f, field_h * 0.5f},
        paddle_shape, AwBodyType::Kinematic, 1);
    if (AwObject* p = world->get(player_id)) {
        p->color = COL_PLAYER;
        p->filled = true;
        p->restitution = 1.0f;
        p->sim_physics = false;
    }

    ai_id = world->create_object("ai",
        {14.f, field_h * 0.5f},
        paddle_shape, AwBodyType::Kinematic, 1);
    if (AwObject* a = world->get(ai_id)) {
        a->color = COL_AI;
        a->filled = true;
        a->restitution = 1.0f;
        a->sim_physics = false;
    }

    // --- ball (dynamic) ---
    AwShape ball_shape = AwShape::make_circle_approx(ball_r, 8);
    ball_id = world->create_object("ball",
        {field_w * 0.5f, field_h * 0.5f},
        ball_shape, AwBodyType::Dynamic, 2);
    if (AwObject* b = world->get(ball_id)) {
        b->color = COL_BALL;
        b->filled = true;
        b->mass = 1.f;
        b->restitution = 1.05f;   // slight energy gain so rallies don't die
        b->friction = 0.f;
        b->sim_physics = true;
    }

    ESP_LOGI(TAG, "World built: ball=%u player=%u ai=%u", ball_id, player_id, ai_id);
}

void PongApp::serve_ball(bool toward_player) {
    if (!world) return;
    AwObject* b = world->get(ball_id);
    if (!b) return;

    b->pos = {field_w * 0.5f, field_h * 0.5f};
    world->update_world_aabb(*b);

    // Slight vertical bias so serves aren't boring
    float vy = ((int)(esp_timer_get_time() & 1) ? 1.f : -1.f) * (ball_speed * 0.35f);
    float vx = toward_player ? ball_speed : -ball_speed;
    b->velocity = {vx, vy};
    b->angular_velocity = 0.f;
    running = true;
}

void PongApp::reset_round() {
    // End previous rally streak → count as a played game
    if (score_player > 0 || score_ai > 0) {
        game_stats::record_round(kStatsName, stats, (uint32_t)score_player);
    }
    score_player = 0;
    score_ai = 0;
    hold_up = hold_down = false;
    serve_ball(true);
    update_score_text();
}

// ---------------------------------------------------------------------------
// Player paddle
// ---------------------------------------------------------------------------

void PongApp::update_player(float dt) {
    AwObject* p = world ? world->get(player_id) : nullptr;
    if (!p) return;

    float dy = 0.f;
    if (hold_up)   dy -= player_speed * dt;
    if (hold_down) dy += player_speed * dt;

    if (dy != 0.f) {
        float ny = p->pos.y + dy;
        // Clamp so paddle stays fully inside field
        float min_y = paddle_hh;
        float max_y = field_h - paddle_hh;
        if (ny < min_y) ny = min_y;
        if (ny > max_y) ny = max_y;
        world->set_position(player_id, {p->pos.x, ny});
    }
}

// ---------------------------------------------------------------------------
// AI – predict up to 2 wall rebounds, chase intercept at AI x
// ---------------------------------------------------------------------------

void PongApp::predict_ball_path() {
    predict_count = 0;
    AwObject* b = world ? world->get(ball_id) : nullptr;
    if (!b || !running) return;

    AwVec2 pos = b->pos;
    AwVec2 vel = b->velocity;
    if (vel.length_sq() < 1.f) return;

    // Only care when ball is moving toward the AI (left)
    // Still compute full path for debug draw either way.
    predict_pts[predict_count++] = pos;

    const float y_lo = ball_r;
    const float y_hi = field_h - ball_r;
    const float ai_x = 14.f + paddle_hw;   // face of AI paddle

    int bounces = 0;
    const int max_bounces = 2;
    const int max_iters = 8;

    for (int iter = 0; iter < max_iters && predict_count < PREDICT_MAX_PTS; ++iter) {
        if (fabsf(vel.x) < 1e-3f && fabsf(vel.y) < 1e-3f) break;

        // Time to hit top or bottom
        float t_wall = 1e9f;
        int wall = 0; // +1 top (y_hi), -1 bottom (y_lo)
        if (vel.y > 1e-4f) {
            t_wall = (y_hi - pos.y) / vel.y;
            wall = 1;
        } else if (vel.y < -1e-4f) {
            t_wall = (y_lo - pos.y) / vel.y;
            wall = -1;
        }

        // Time to reach AI paddle plane (or player plane for completeness)
        float target_x = (vel.x < 0.f) ? ai_x : (field_w - 14.f - paddle_hw);
        float t_paddle = 1e9f;
        if (fabsf(vel.x) > 1e-4f) {
            t_paddle = (target_x - pos.x) / vel.x;
            if (t_paddle < 0.f) t_paddle = 1e9f;
        }

        if (t_paddle <= t_wall && t_paddle < 1e8f) {
            // Hits paddle plane first – final intercept
            AwVec2 hit = pos + vel * t_paddle;
            if (hit.y < y_lo) hit.y = y_lo;
            if (hit.y > y_hi) hit.y = y_hi;
            predict_pts[predict_count++] = hit;
            break;
        }

        if (t_wall >= 1e8f) {
            // No wall hit, extend a bit
            AwVec2 hit = pos + vel * 0.5f;
            predict_pts[predict_count++] = hit;
            break;
        }

        // Bounce off top/bottom
        AwVec2 hit = pos + vel * t_wall;
        hit.y = (wall > 0) ? y_hi : y_lo;
        predict_pts[predict_count++] = hit;
        pos = hit;
        vel.y = -vel.y;
        ++bounces;
        if (bounces >= max_bounces && predict_count < PREDICT_MAX_PTS) {
            // After 2 rebounds, project one more segment toward paddle plane
            if (fabsf(vel.x) > 1e-4f) {
                float t = (target_x - pos.x) / vel.x;
                if (t > 0.f) {
                    AwVec2 final_pt = pos + vel * t;
                    if (final_pt.y < y_lo) final_pt.y = y_lo;
                    if (final_pt.y > y_hi) final_pt.y = y_hi;
                    predict_pts[predict_count++] = final_pt;
                }
            }
            break;
        }
    }
}

void PongApp::update_ai(float dt) {
    AwObject* a = world ? world->get(ai_id) : nullptr;
    AwObject* b = world ? world->get(ball_id) : nullptr;
    if (!a || !b) return;

    predict_ball_path();

    // Target Y: last predicted point if ball heading left, else track ball
    float target_y = b->pos.y;
    if (b->velocity.x < 0.f && predict_count >= 2) {
        target_y = predict_pts[predict_count - 1].y;
    }

    float err = target_y - a->pos.y;
    // Deadzone so AI doesn't jitter
    if (fabsf(err) < 3.f) return;

    float max_step = ai_max_speed * dt;
    float step = err;
    if (step >  max_step) step =  max_step;
    if (step < -max_step) step = -max_step;

    float ny = a->pos.y + step;
    float min_y = paddle_hh;
    float max_y = field_h - paddle_hh;
    if (ny < min_y) ny = min_y;
    if (ny > max_y) ny = max_y;
    world->set_position(ai_id, {a->pos.x, ny});
}

// ---------------------------------------------------------------------------
// Goals
// ---------------------------------------------------------------------------

void PongApp::check_goals() {
    AwObject* b = world ? world->get(ball_id) : nullptr;
    if (!b || !running) return;

    // Ball fully past left edge → player scores
    if (b->world_aabb_max_x < 0.f) {
        score_player++;
        ESP_LOGI(TAG, "Player scores! %d – %d", score_player, score_ai);
        // Track player points as the "score" for high-score table
        if ((uint32_t)score_player > stats.high_score) {
            stats.high_score = (uint32_t)score_player;
            game_stats::save(kStatsName, stats);
        }
        running = false;
        serve_ball(false);   // serve toward AI
        update_score_text();
        return;
    }
    // Past right edge → AI scores
    if (b->world_aabb_min_x > field_w) {
        score_ai++;
        ESP_LOGI(TAG, "AI scores! %d – %d", score_player, score_ai);
        running = false;
        serve_ball(true);
        update_score_text();
        return;
    }
}

// ---------------------------------------------------------------------------
// Debug prediction lines
// ---------------------------------------------------------------------------

void PongApp::draw_debug_prediction() {
    if (!show_prediction || predict_count < 2 || !pong_window || !pong_canvas)
        return;

    const AwCamera& cam = pong_canvas->GetWorldCamera();

    for (int i = 0; i + 1 < predict_count; ++i) {
        int x0, y0, x1, y1;
        AnimWorld::world_to_pixel(cam, predict_pts[i].x,     predict_pts[i].y,     x0, y0);
        AnimWorld::world_to_pixel(cam, predict_pts[i + 1].x, predict_pts[i + 1].y, x1, y1);

        int sx0, sy0, sx1, sy1;
        pong_window->LocalToScreen(x0, y0, sx0, sy0);
        pong_window->LocalToScreen(x1, y1, sx1, sy1);
        fb_line(sx0, sy0, sx1, sy1, COL_PRED);
    }
}

// ---------------------------------------------------------------------------
// Score text (window top strip)
// ---------------------------------------------------------------------------

void PongApp::update_score_text() {
    if (!pong_window) return;
    char buf[192];
    snprintf(buf, sizeof(buf),
             "<|size=2|><|color=0xF800|>AI %d<|color=0xFFFF|>  –  <|color=0x07E0|>%d YOU"
             "  <|color=0x8888|>HI %u<|n|>"
             "<|size=1|><|color=0x8888|>UP/DN=paddle  ENTER=pause  BACK=menu",
             score_ai, score_player, (unsigned)stats.high_score);
    pong_window->SetText(buf);
}

// ---------------------------------------------------------------------------
// Tick / draw
// ---------------------------------------------------------------------------

void PongApp::tick_app(uint32_t delta_ms) {
    if (!world || paused) {
        if (pong_window) pong_window->dirty = true;
        return;
    }

    float dt = delta_ms / 1000.f;
    if (dt > 0.05f) dt = 0.05f;

    update_player(dt);
    update_ai(dt);

    if (running) {
        world->step(dt);

        // Manual top/bottom bounce (left/right are goals, not walls)
        if (AwObject* b = world->get(ball_id)) {
            const float y_lo = ball_r;
            const float y_hi = field_h - ball_r;
            if (b->pos.y < y_lo) {
                b->pos.y = y_lo;
                if (b->velocity.y < 0.f) b->velocity.y = -b->velocity.y;
                world->update_world_aabb(*b);
            } else if (b->pos.y > y_hi) {
                b->pos.y = y_hi;
                if (b->velocity.y > 0.f) b->velocity.y = -b->velocity.y;
                world->update_world_aabb(*b);
            }

            // Reflect off paddles (engine pair resolve can miss fast tunnels)
            auto overlaps = [](const AwObject& a, const AwObject& p) {
                return !(a.world_aabb_max_x < p.world_aabb_min_x ||
                         a.world_aabb_min_x > p.world_aabb_max_x ||
                         a.world_aabb_max_y < p.world_aabb_min_y ||
                         a.world_aabb_min_y > p.world_aabb_max_y);
            };
            auto bounce_paddle = [&](uint16_t pid, bool player_side) {
                AwObject* p = world->get(pid);
                if (!p || !b) return;
                if (!overlaps(*b, *p)) return;
                // Only bounce when moving toward this paddle
                if (player_side && b->velocity.x <= 0.f) return;
                if (!player_side && b->velocity.x >= 0.f) return;

                b->velocity.x = -b->velocity.x;
                // English from hit offset
                float rel = (b->pos.y - p->pos.y) / (paddle_hh + ball_r);
                if (rel > 1.f) rel = 1.f;
                if (rel < -1.f) rel = -1.f;
                b->velocity.y += rel * 60.f;
                // Nudge out of paddle
                if (player_side)
                    b->pos.x = p->world_aabb_min_x - ball_r - 0.5f;
                else
                    b->pos.x = p->world_aabb_max_x + ball_r + 0.5f;
                world->update_world_aabb(*b);
            };
            bounce_paddle(player_id, true);
            bounce_paddle(ai_id, false);
        }

        check_goals();
    }

    // Force full window redraw every tick so canvas sprites stay on screen
    if (pong_window) pong_window->dirty = true;
}

void PongApp::on_draw() {
    if (!pong_window) return;

    // Score text only here. Paddles/ball are drawn inside Window::WinDraw()
    // via DrawCanvas() → Canvas::Draw() → AnimWorld::draw(), AFTER the
    // solid background fill (so they are not wiped).
    // Prediction lines are drawn in tick after WM has painted, if needed;
    // for reliability we also mark dirty so WinDraw runs every frame.
    pong_window->dirty = true;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void PongApp::receive_event_input(const void* event) {
    if (!event) return;
    const InputEvent* ev = static_cast<const InputEvent*>(event);

    // Right dial / UP-DOWN: Hold = continuous motion in tick_app;
    // Tap = one discrete step (works on stacks that never send Hold).
    const float tap_step = 12.f;

    auto nudge_player = [&](float dy) {
        if (!world) return;
        AwObject* p = world->get(player_id);
        if (!p) return;
        float ny = p->pos.y + dy;
        float min_y = paddle_hh;
        float max_y = field_h - paddle_hh;
        if (ny < min_y) ny = min_y;
        if (ny > max_y) ny = max_y;
        world->set_position(player_id, {p->pos.x, ny});
        if (pong_window) pong_window->dirty = true;
    };

    if (ev->key == KEY_UP) {
        if (ev->action == KeyAction::Hold) {
            hold_up = true;
            hold_down = false;
        } else if (ev->action == KeyAction::Tap) {
            hold_up = hold_down = false;
            nudge_player(-tap_step);
        }
        return;
    }
    if (ev->key == KEY_DOWN) {
        if (ev->action == KeyAction::Hold) {
            hold_down = true;
            hold_up = false;
        } else if (ev->action == KeyAction::Tap) {
            hold_up = hold_down = false;
            nudge_player(+tap_step);
        }
        return;
    }

    if (ev->action == KeyAction::Hold && ev->key == KEY_ENTER) {
        reset_round();
        on_draw();
        return;
    }

    if (ev->action != KeyAction::Tap) return;

    switch (ev->key) {
        case KEY_ENTER:
            paused = !paused;
            update_score_text();
            on_draw();
            break;

        case KEY_BACK:
            appManager::instance().close_current_and_open("MenuApp");
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_pong() {
    AppManifest m;
    m.name = "PongApp";
    m.display_name = "Pong";
    m.description = "Pong – AnimWorld paddles, AI with 2-bounce prediction";
    m.capabilities = static_cast<uint32_t>(AppCapability::FULLSCREEN) |
                     static_cast<uint32_t>(AppCapability::NEEDS_WINDOW);
    m.stack_size_bytes = 10240;
    m.priority = 5;
    m.tick_rate_hz = 30;
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<PongApp>(cfg);
    };

    appManager::instance().register_app(m);
}
