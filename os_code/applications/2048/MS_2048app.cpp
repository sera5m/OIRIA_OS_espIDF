#include "MS_2048app.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"

static const char* TAG = "Game2048";

static constexpr uint16_t COL_BG   = 0x9CD3;  // board beige-ish in RGB565
static constexpr uint16_t COL_EMPTY= 0xBDF7;
static constexpr uint16_t COL_TEXT = 0xFFFF;

// ---------------------------------------------------------------------------

Game2048App::Game2048App(const ApplicationConfig& cfg) : AppBase(cfg) {
    appTickRateHZ = 20;
}

void Game2048App::on_start() {
    ESP_LOGI(TAG, "2048 starting");

    field_w = N * cell + (N + 1) * gap;
    field_h = field_w;

    win = std::make_shared<Window>(
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
            .BorderColor = 0xC618,
            .BgColor = 0x4208,
            .Bg_secondaryColor = 0x2104,
            .WinTextColor = COL_TEXT,
            .backgroundType = BgFillType::Solid,
            .UpdateRate = 0.5f
        },
        "2048"
    );

    WindowManager::getInstance().registerWindow(win);
    bind_main_window(win);

    CanvasCfg cc;
    cc.x = (int)((280.f - field_w) * 0.5f);
    if (cc.x < 0) cc.x = 4;
    cc.y = 30;
    cc.width  = (int)field_w + 2;
    cc.height = (int)field_h + 2;
    cc.borderless = true;
    cc.DrawBG = false;
    cc.parentWindow = win.get();
    canvas = win->AddCanvas(cc);

    build_world();

    if (canvas && world) {
        canvas->AttachWorld(world.get());
        AwCamera cam;
        cam.origin = {0.f, 0.f};
        cam.scale = 1.f;
        cam.offset_x = cc.x;
        cam.offset_y = cc.y;
        canvas->SetWorldCamera(cam);
        canvas->SetAutoDrawWorld(true);
    }

    game_stats::load(kStatsName, stats);
    reset_game();
    win->dirty = true;
}

void Game2048App::on_stop() {
    // Persist best score for this session even if not game-over yet
    if (score > 0 && (uint32_t)score > stats.high_score) {
        stats.high_score = (uint32_t)score;
        stats.last_score = (uint32_t)score;
        game_stats::save(kStatsName, stats);
    } else if (score > 0) {
        stats.last_score = (uint32_t)score;
        game_stats::save(kStatsName, stats);
    }
    if (canvas) {
        canvas->DetachWorld();
        canvas.reset();
    }
    world.reset();
    if (win) {
        WindowManager::getInstance().unregisterWindow(win);
        win.reset();
    }
}

void Game2048App::on_pause()  {}
void Game2048App::on_resume() {}

// ---------------------------------------------------------------------------

AwVec2 Game2048App::cell_center(int c, int r) const {
    float x = gap + c * (cell + gap) + cell * 0.5f;
    float y = gap + r * (cell + gap) + cell * 0.5f;
    return {x, y};
}

uint16_t Game2048App::color_for(int value) const {
    // Rough palette by magnitude
    switch (value) {
        case 2:    return 0xEF7D;
        case 4:    return 0xEF5B;
        case 8:    return 0xFBE4;
        case 16:   return 0xFBC0;
        case 32:   return 0xFB80;
        case 64:   return 0xFB40;
        case 128:  return 0xFFEC;
        case 256:  return 0xFEE8;
        case 512:  return 0xFE65;
        case 1024: return 0xFE02;
        case 2048: return 0xFDE0;
        default:   return 0x8410;
    }
}

void Game2048App::build_world() {
    AwWorldConfig cfg;
    cfg.border.type = AwBorderType::None;
    cfg.enable_border_collisions = false;
    cfg.enable_object_collisions = false;
    cfg.gravity = {0.f, 0.f};
    world = std::make_unique<AnimWorld>(cfg);

    // Empty slots – visuals built in rebuild_visuals via create_welded_tile
    for (int r = 0; r < N; ++r)
        for (int c = 0; c < N; ++c)
            tile_ids[r][c] = 0;
}

void Game2048App::reset_game() {
    for (int r = 0; r < N; ++r)
        for (int c = 0; c < N; ++c) {
            board[r][c] = 0;
            if (tile_ids[r][c] && world) {
                world->destroy_object(tile_ids[r][c]);
                tile_ids[r][c] = 0;
            }
        }
    score = 0;
    won = false;
    lost = false;
    scored_this_round = false;
    spawn_random();
    spawn_random();
    rebuild_visuals();
    update_hud();
}

void Game2048App::spawn_random() {
    int empties[N * N][2];
    int n = 0;
    for (int r = 0; r < N; ++r)
        for (int c = 0; c < N; ++c)
            if (board[r][c] == 0) {
                empties[n][0] = r;
                empties[n][1] = c;
                n++;
            }
    if (n == 0) return;
    uint32_t t = (uint32_t)(esp_timer_get_time() & 0xFFFFFFFF);
    int pick = (int)(t % (uint32_t)n);
    int r = empties[pick][0];
    int c = empties[pick][1];
    // 90% → 2, 10% → 4
    board[r][c] = ((t / 7) % 10 == 0) ? 4 : 2;
}

bool Game2048App::can_move() const {
    for (int r = 0; r < N; ++r)
        for (int c = 0; c < N; ++c) {
            if (board[r][c] == 0) return true;
            if (c + 1 < N && board[r][c] == board[r][c + 1]) return true;
            if (r + 1 < N && board[r][c] == board[r + 1][c]) return true;
        }
    return false;
}

// Slide/merge in direction (dx,dy). One axis must be 0.
bool Game2048App::slide(int dx, int dy) {
    bool moved = false;
    bool merged[N][N] = {{false}};

    // Process lines against the direction of travel
    auto process_line = [&](int r0, int c0, int step_r, int step_c) {
        int vals[N];
        int count = 0;
        // Gather non-zero in order from "far" side toward slide direction... 
        // Actually gather from the side we slide TOWARD (destination first).
        // For left (dx=-1): columns 0..3, merge leftward.
        for (int i = 0; i < N; ++i) {
            int r = r0 + i * step_r;
            int c = c0 + i * step_c;
            if (board[r][c] != 0)
                vals[count++] = board[r][c];
        }
        // Merge adjacent equals
        int out[N] = {0};
        int o = 0;
        for (int i = 0; i < count; ) {
            if (i + 1 < count && vals[i] == vals[i + 1]) {
                out[o++] = vals[i] * 2;
                score += vals[i] * 2;
                if (vals[i] * 2 >= 2048) won = true;
                i += 2;
            } else {
                out[o++] = vals[i];
                i += 1;
            }
        }
        // Write back
        for (int i = 0; i < N; ++i) {
            int r = r0 + i * step_r;
            int c = c0 + i * step_c;
            int nv = (i < o) ? out[i] : 0;
            if (board[r][c] != nv) moved = true;
            board[r][c] = nv;
        }
    };

    if (dx == -1 && dy == 0) {          // left
        for (int r = 0; r < N; ++r)
            process_line(r, 0, 0, 1);
    } else if (dx == 1 && dy == 0) {    // right
        for (int r = 0; r < N; ++r)
            process_line(r, N - 1, 0, -1);
    } else if (dx == 0 && dy == -1) {   // up
        for (int c = 0; c < N; ++c)
            process_line(0, c, 1, 0);
    } else if (dx == 0 && dy == 1) {    // down
        for (int c = 0; c < N; ++c)
            process_line(N - 1, c, -1, 0);
    }

    (void)merged;
    return moved;
}

void Game2048App::rebuild_visuals() {
    if (!world) return;

    // Destroy old value tiles (keep empty backgrounds)
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            if (tile_ids[r][c]) {
                world->destroy_object(tile_ids[r][c]);
                tile_ids[r][c] = 0;
            }
        }
    }

    // Empty-cell backgrounds (once per world lifetime)
    if (empty_ids[0][0] == 0) {
        AwShape bg = AwShape::make_rect(cell * 0.48f, cell * 0.48f);
        for (int r = 0; r < N; ++r) {
            for (int c = 0; c < N; ++c) {
                char name[12];
                snprintf(name, sizeof(name), "e%d%d", r, c);
                empty_ids[r][c] = world->create_object(
                    name, cell_center(c, r), bg, AwBodyType::Static, 0);
                if (AwObject* o = world->get(empty_ids[r][c])) {
                    o->color = COL_EMPTY;
                    o->filled = true;
                    o->sim_physics = false;
                    o->visible = true;
                    world->update_world_aabb(*o);
                }
            }
        }
    }

    // Value tiles with welded labels
    float half = cell * 0.42f;
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            int v = board[r][c];
            if (v == 0) continue;

            char name[12];
            char label[8];
            snprintf(name, sizeof(name), "t%d%d", r, c);
            snprintf(label, sizeof(label), "%d", v);

            uint16_t id = world->create_welded_tile(
                name,
                cell_center(c, r),
                half, half,
                label,
                color_for(v),
                (v <= 4) ? (uint16_t)0x4208 : (uint16_t)0xFFFF,
                {0.f, 0.f}
            );
            tile_ids[r][c] = id;

            if (id) {
                uint16_t tid = world->find_text_child(id);
                if (AwObject* t = world->get(tid)) {
                    t->text_size = (v >= 1000) ? 1 : 2;
                }
            }
        }
    }
}

void Game2048App::update_hud() {
    if (!win) return;
    char buf[192];
    if (lost) {
        snprintf(buf, sizeof(buf),
                 "<|size=2|><|color=0xF800|>GAME OVER  <|color=0xFFFF|>%d"
                 "  <|color=0x8888|>HI %u<|n|>"
                 "<|size=1|><|color=0x8888|>HOLD-ENTER=new  BACK=menu",
                 score, (unsigned)stats.high_score);
    } else if (won) {
        snprintf(buf, sizeof(buf),
                 "<|size=2|><|color=0xFDE0|>2048!  <|color=0xFFFF|>%d"
                 "  HI %u  (keep going)<|n|>"
                 "<|size=1|><|color=0x8888|>D-pad=slide  HOLD-ENTER=new",
                 score, (unsigned)stats.high_score);
    } else {
        snprintf(buf, sizeof(buf),
                 "<|size=2|>2048   <|color=0xFDE0|>%d  <|color=0x8888|>HI %u<|n|>"
                 "<|size=1|><|color=0x8888|>D-pad=slide  HOLD-ENTER=new  BACK=menu",
                 score, (unsigned)stats.high_score);
    }
    win->SetText(buf);
}

// ---------------------------------------------------------------------------

void Game2048App::tick_app(uint32_t delta_ms) {
    (void)delta_ms;
    if (win) win->dirty = true;
}

void Game2048App::on_draw() {
    if (win) win->dirty = true;
}

void Game2048App::receive_event_input(const void* event) {
    if (!event) return;
    const InputEvent* ev = static_cast<const InputEvent*>(event);

    if (ev->action == KeyAction::Hold && ev->key == KEY_ENTER) {
        reset_game();
        return;
    }

    // Accept Tap and Hold so dial ticks register reliably
    if (ev->action != KeyAction::Tap && ev->action != KeyAction::Hold)
        return;

    if (ev->key == KEY_BACK) {
        appManager::instance().close_current_and_open("MenuApp");
        return;
    }

    if (lost) return;

    // win_rotation = 1 (90° CW). On-wrist right dial is KEY_UP/KEY_DOWN and
    // should move the *board* vertically; left dial KEY_LEFT/RIGHT horizontally.
    // If your HID already rotates events, this still maps 1:1 to board axes.
    int dx = 0, dy = 0;
    switch (ev->key) {
        case KEY_UP:    dy = -1; break;
        case KEY_DOWN:  dy =  1; break;
        case KEY_LEFT:  dx = -1; break;
        case KEY_RIGHT: dx =  1; break;
        default: return;
    }

    // Discrete moves on Tap; slow auto-repeat on Hold so one tick ≠ flood
    static uint32_t last_slide_ms = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (ev->action == KeyAction::Hold && (now - last_slide_ms) < 220)
        return;
    if (ev->action == KeyAction::Tap && (now - last_slide_ms) < 80)
        return;

    ESP_LOGD(TAG, "slide dx=%d dy=%d action=%d", dx, dy, (int)ev->action);
    if (slide(dx, dy)) {
        last_slide_ms = now;
        spawn_random();
        rebuild_visuals();
        if ((uint32_t)score > stats.high_score) {
            stats.high_score = (uint32_t)score;
            game_stats::save(kStatsName, stats);
        }
        if (!can_move()) {
            lost = true;
            if (!scored_this_round) {
                scored_this_round = true;
                game_stats::record_round(kStatsName, stats, (uint32_t)score);
            }
            ESP_LOGI(TAG, "No moves left. score=%d", score);
        }
        update_hud();
        if (win) win->dirty = true;
    }
}

void register_2048() {
    AppManifest m;
    m.name = "Game2048App";
    m.display_name = "2048";
    m.description = "2048 with welded AnimWorld tiles";
    m.capabilities = static_cast<uint32_t>(AppCapability::FULLSCREEN) |
                     static_cast<uint32_t>(AppCapability::NEEDS_WINDOW);
    m.stack_size_bytes = 12288;
    m.priority = 5;
    m.tick_rate_hz = 20;
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<Game2048App>(cfg);
    };
    appManager::instance().register_app(m);
}
