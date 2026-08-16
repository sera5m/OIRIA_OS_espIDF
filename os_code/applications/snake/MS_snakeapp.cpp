#include "MS_snakeapp.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"

static const char* TAG = "SnakeApp";

// Light playfield so BLACK grid is visible
static constexpr uint16_t COL_BG     = 0xC618;  // light grey
static constexpr uint16_t COL_HEAD   = 0x07E0;
static constexpr uint16_t COL_BODY   = 0x04B0;
static constexpr uint16_t COL_FOOD   = 0xF800;
static constexpr uint16_t COL_TEXT   = 0x0000;
static constexpr uint16_t COL_GRID   = 0x0000;  // black grid

SnakeApp::SnakeApp(const ApplicationConfig& cfg) : AppBase(cfg) {
    appTickRateHZ = 30;
}

void SnakeApp::on_start() {
    ESP_LOGI(TAG, "Snake starting");

    field_w = GRID_W * cell;
    field_h = GRID_H * cell;

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
            .BorderColor = 0x0000,
            .BgColor = 0xEF5D,
            .Bg_secondaryColor = 0xC618,
            .WinTextColor = COL_TEXT,
            .backgroundType = BgFillType::Solid,
            .UpdateRate = 0.5f
        },
        "Snake"
    );

    WindowManager::getInstance().registerWindow(win);
    bind_main_window(win);

    CanvasCfg cc;
    cc.x = 8;
    cc.y = 28;
    cc.width  = (int)field_w + 1;
    cc.height = (int)field_h + 1;
    cc.borderless = true;
    cc.DrawBG = true;
    cc.bgColor = COL_BG;
    cc.parentWindow = win.get();
    canvas = win->AddCanvas(cc);

    build_world();
    build_grid_overlay();

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
    update_hud();
    if (win) win->dirty = true;
}

void SnakeApp::on_stop() {
    if (running && !scored_this_round && score > 0)
        game_stats::record_round(kStatsName, stats, (uint32_t)score);
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

void SnakeApp::on_pause()  { paused = true; }
void SnakeApp::on_resume() { paused = false; if (win) win->dirty = true; }

AwVec2 SnakeApp::cell_to_world(int cx, int cy) const {
    return { (cx + 0.5f) * cell, (cy + 0.5f) * cell };
}

void SnakeApp::build_grid_overlay() {
    if (!canvas) return;
    // Black grid lines in canvas local space (shapes draw before world).
    for (int x = 0; x <= GRID_W; ++x) {
        s_bounds_16u b{};
        b.x = (uint16_t)(x * cell);
        b.y = 0;
        b.w = 1;
        b.h = (uint16_t)field_h;
        canvas->AddShape(SHAPE_LINE, b, COL_GRID, 0);
    }
    for (int y = 0; y <= GRID_H; ++y) {
        s_bounds_16u b{};
        b.x = 0;
        b.y = (uint16_t)(y * cell);
        b.w = (uint16_t)field_w;
        b.h = 1;
        canvas->AddShape(SHAPE_LINE, b, COL_GRID, 0);
    }
}

bool SnakeApp::ensure_seg_objects(int need) {
    if (!world) return false;
    if (need > MAX_SEG_OBJECTS) need = MAX_SEG_OBJECTS;
    if (seg_object_count >= need) return true;

    AwShape sq = AwShape::make_rect(cell * 0.42f, cell * 0.42f);
    while (seg_object_count < need) {
        char name[12];
        snprintf(name, sizeof(name), "s%d", seg_object_count);
        uint16_t id = world->create_object(name, {-100.f, -100.f}, sq,
                                           AwBodyType::Kinematic, 1);
        if (!id) {
            ESP_LOGW(TAG, "Cannot grow segment pool past %d", seg_object_count);
            return seg_object_count > 0;
        }
        seg_ids[seg_object_count] = id;
        if (AwObject* o = world->get(id)) {
            o->color = COL_BODY;
            o->filled = true;
            o->sim_physics = false;
            o->visible = false;
        }
        seg_object_count++;
    }
    return true;
}

void SnakeApp::build_world() {
    AwWorldConfig cfg;
    cfg.border.type = AwBorderType::None;
    cfg.enable_border_collisions = false;
    cfg.enable_object_collisions = false;
    cfg.gravity = {0.f, 0.f};
    world = std::make_unique<AnimWorld>(cfg);

    seg_object_count = 0;
    for (int i = 0; i < MAX_SEG_OBJECTS; ++i) seg_ids[i] = 0;

    // Small initial pool — grow on demand when the snake eats
    ensure_seg_objects(INITIAL_SEG_OBJECTS);

    AwShape sq = AwShape::make_rect(cell * 0.42f, cell * 0.42f);
    food_id = world->create_object("food", {-100.f, -100.f}, sq,
                                   AwBodyType::Kinematic, 2);
    if (AwObject* f = world->get(food_id)) {
        f->color = COL_FOOD;
        f->filled = true;
        f->sim_physics = false;
        f->visible = false;
    }
    ESP_LOGI(TAG, "Snake pool: %d segments (max %d) + food",
             seg_object_count, MAX_SEG_OBJECTS);
}

void SnakeApp::reset_game() {
    length = 3;
    gx[0] = GRID_W / 2; gy[0] = GRID_H / 2;
    gx[1] = gx[0] - 1;  gy[1] = gy[0];
    gx[2] = gx[0] - 2;  gy[2] = gy[0];
    dir_x = 1; dir_y = 0;
    pending_dx = 1; pending_dy = 0;
    score = 0;
    game_over = false;
    scored_this_round = false;
    running = true;
    paused = false;
    accum_ms = 0;
    step_ms = 180;
    spawn_food();
    sync_sprites();
    update_hud();
}

void SnakeApp::spawn_food() {
    for (int attempt = 0; attempt < 200; ++attempt) {
        int fx = (int)((esp_timer_get_time() / 17 + attempt * 37) % GRID_W);
        int fy = (int)((esp_timer_get_time() / 31 + attempt * 13) % GRID_H);
        if (fx < 0) fx = -fx;
        if (fy < 0) fy = -fy;
        fx %= GRID_W; fy %= GRID_H;

        bool occ = false;
        for (int i = 0; i < length; ++i)
            if (gx[i] == fx && gy[i] == fy) { occ = true; break; }
        if (occ) continue;

        food_x = (int8_t)fx;
        food_y = (int8_t)fy;
        if (AwObject* f = world->get(food_id)) {
            f->pos = cell_to_world(fx, fy);
            f->visible = true;
            world->update_world_aabb(*f);
        }
        return;
    }
}

void SnakeApp::sync_sprites() {
    if (!world) return;
    // Only the first min(length, seg_object_count) cells are drawn.
    // Extra pool slots stay parked off-screen and invisible.
    int draw_n = length;
    if (draw_n > seg_object_count) draw_n = seg_object_count;
    for (int i = 0; i < seg_object_count; ++i) {
        AwObject* o = world->get(seg_ids[i]);
        if (!o) continue;
        if (i < draw_n) {
            o->pos = cell_to_world(gx[i], gy[i]);
            o->color = (i == 0) ? COL_HEAD : COL_BODY;
            o->visible = true;
            world->update_world_aabb(*o);
        } else {
            o->visible = false;
            o->pos = {-100.f, -100.f};
        }
    }
}

// Rotate heading: side -1 = left, +1 = right relative to (dir_x, dir_y)
void SnakeApp::turn_relative(int side) {
    // left:  (dx,dy) -> (-dy, dx)
    // right: (dx,dy) -> ( dy,-dx)
    int8_t ndx, ndy;
    if (side < 0) {
        ndx = (int8_t)(-dir_y);
        ndy = dir_x;
    } else {
        ndx = dir_y;
        ndy = (int8_t)(-dir_x);
    }
    // Never reverse 180 in one step is automatic for 90° turns
    pending_dx = ndx;
    pending_dy = ndy;
}

void SnakeApp::step_snake() {
    if (!running || paused || game_over) return;

    dir_x = pending_dx;
    dir_y = pending_dy;

    int nx = gx[0] + dir_x;
    int ny = gy[0] + dir_y;

    if (nx < 0 || ny < 0 || nx >= GRID_W || ny >= GRID_H) {
        game_over = true;
        running = false;
        if (!scored_this_round) {
            scored_this_round = true;
            game_stats::record_round(kStatsName, stats, (uint32_t)score);
        }
        update_hud();
        return;
    }

    for (int i = 0; i < length; ++i) {
        if (gx[i] == nx && gy[i] == ny) {
            game_over = true;
            running = false;
            if (!scored_this_round) {
                scored_this_round = true;
                game_stats::record_round(kStatsName, stats, (uint32_t)score);
            }
            update_hud();
            return;
        }
    }

    bool ate = (nx == food_x && ny == food_y);

    for (int i = length - 1; i > 0; --i) {
        gx[i] = gx[i - 1];
        gy[i] = gy[i - 1];
    }
    gx[0] = (int8_t)nx;
    gy[0] = (int8_t)ny;

    if (ate) {
        // Grow logical length; pull a sprite from the pool (create if needed)
        if (length < MAX_LEN) {
            if (length >= seg_object_count)
                ensure_seg_objects(seg_object_count + 4);
            if (length < seg_object_count) {
                gx[length] = gx[length - 1];
                gy[length] = gy[length - 1];
                length++;
            }
            // If pool is exhausted we still score but stop growing sprites
        }
        score += 10;
        if (step_ms > 80) step_ms -= 4;
        spawn_food();
    }

    sync_sprites();
    update_hud();
}

void SnakeApp::update_hud() {
    if (!win) return;
    char buf[192];
    if (game_over) {
        snprintf(buf, sizeof(buf),
                 "<|size=2|><|color=0xF800|>GAME OVER %d  HI %u<|n|>"
                 "<|size=1|>HOLD-ENTER=retry  BACK=menu",
                 score, (unsigned)stats.high_score);
    } else if (paused) {
        snprintf(buf, sizeof(buf),
                 "<|size=2|>SNAKE %d HI %u PAUSED<|n|>"
                 "<|size=1|>UP/DN=turn L/R  ENTER=resume",
                 score, (unsigned)stats.high_score);
    } else {
        snprintf(buf, sizeof(buf),
                 "<|size=2|>SNAKE %d HI %u<|n|>"
                 "<|size=1|>UP=L-turn DN=R-turn  ENTER=pause",
                 score, (unsigned)stats.high_score);
    }
    win->SetText(buf);
}

void SnakeApp::tick_app(uint32_t delta_ms) {
    if (!world) return;
    if (!paused && running && !game_over) {
        accum_ms += delta_ms;
        while (accum_ms >= step_ms) {
            accum_ms -= step_ms;
            step_snake();
        }
    }
    if (win) win->dirty = true;
}

void SnakeApp::on_draw() {
    if (win) win->dirty = true;
}

void SnakeApp::receive_event_input(const void* event) {
    if (!event) return;
    const InputEvent* ev = static_cast<const InputEvent*>(event);

    if (ev->action == KeyAction::Hold && ev->key == KEY_ENTER) {
        reset_game();
        return;
    }

    if (ev->action != KeyAction::Tap && ev->action != KeyAction::Hold)
        return;

    switch (ev->key) {
        case KEY_UP:
            // One-dial: UP = turn LEFT relative to current heading
            turn_relative(-1);
            break;
        case KEY_DOWN:
            // One-dial: DOWN = turn RIGHT relative to current heading
            turn_relative(+1);
            break;
        case KEY_LEFT:
            // Absolute cardinal (second dial / optional)
            if (dir_x == 0) { pending_dx = -1; pending_dy = 0; }
            break;
        case KEY_RIGHT:
            if (dir_x == 0) { pending_dx = 1; pending_dy = 0; }
            break;
        case KEY_ENTER:
            if (game_over) reset_game();
            else {
                paused = !paused;
                update_hud();
            }
            break;
        case KEY_BACK:
            appManager::instance().close_current_and_open("MenuApp");
            break;
        default:
            break;
    }
}

void register_snake() {
    AppManifest m;
    m.name = "SnakeApp";
    m.display_name = "Snake";
    m.description = "Snake – dial turns relative to heading, black grid";
    m.capabilities = static_cast<uint32_t>(AppCapability::FULLSCREEN) |
                     static_cast<uint32_t>(AppCapability::NEEDS_WINDOW);
    m.stack_size_bytes = 16384;
    m.priority = 5;
    m.tick_rate_hz = 30;
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<SnakeApp>(cfg);
    };
    appManager::instance().register_app(m);
}
