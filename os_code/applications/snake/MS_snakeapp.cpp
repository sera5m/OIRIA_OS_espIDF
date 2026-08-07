#include "MS_snakeapp.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"

static const char* TAG = "SnakeApp";

static constexpr uint16_t COL_BG     = 0x10A2;
static constexpr uint16_t COL_HEAD   = 0x07E0;
static constexpr uint16_t COL_BODY   = 0x04B0;
static constexpr uint16_t COL_FOOD   = 0xF800;
static constexpr uint16_t COL_TEXT   = 0xFFFF;

// ---------------------------------------------------------------------------

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
            .BorderColor = 0xC618,
            .BgColor = COL_BG,
            .Bg_secondaryColor = 0x2104,
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
    cc.width  = (int)field_w;
    cc.height = (int)field_h;
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
    update_hud();
    win->dirty = true;
}

void SnakeApp::on_stop() {
    if (running && !scored_this_round && score > 0) {
        game_stats::record_round(kStatsName, stats, (uint32_t)score);
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

void SnakeApp::on_pause()  { paused = true; }
void SnakeApp::on_resume() { paused = false; }

// ---------------------------------------------------------------------------

AwVec2 SnakeApp::cell_to_world(int cx, int cy) const {
    // Center of cell
    return { (cx + 0.5f) * cell, (cy + 0.5f) * cell };
}

void SnakeApp::build_world() {
    AwWorldConfig cfg;
    cfg.border.type = AwBorderType::None;
    cfg.enable_border_collisions = false;
    cfg.enable_object_collisions = false;
    cfg.gravity = {0.f, 0.f};
    world = std::make_unique<AnimWorld>(cfg);

    AwShape sq = AwShape::make_rect(cell * 0.45f, cell * 0.45f);

    for (int i = 0; i < MAX_LEN; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "s%d", i);
        seg_ids[i] = world->create_object(name, {0, 0}, sq, AwBodyType::Kinematic, 1);
        if (AwObject* o = world->get(seg_ids[i])) {
            o->color = (i == 0) ? COL_HEAD : COL_BODY;
            o->filled = true;
            o->sim_physics = false;
            o->visible = false;
            o->alive = true;
        }
    }

    food_id = world->create_object("food", {0, 0}, sq, AwBodyType::Kinematic, 2);
    if (AwObject* f = world->get(food_id)) {
        f->color = COL_FOOD;
        f->filled = true;
        f->sim_physics = false;
        f->visible = false;
    }
}

void SnakeApp::reset_game() {
    length = 3;
    gx[0] = GRID_W / 2;     gy[0] = GRID_H / 2;
    gx[1] = gx[0] - 1;      gy[1] = gy[0];
    gx[2] = gx[0] - 2;      gy[2] = gy[0];
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
        for (int i = 0; i < length; ++i) {
            if (gx[i] == fx && gy[i] == fy) { occ = true; break; }
        }
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
    for (int i = 0; i < MAX_LEN; ++i) {
        AwObject* o = world->get(seg_ids[i]);
        if (!o) continue;
        if (i < length) {
            o->pos = cell_to_world(gx[i], gy[i]);
            o->color = (i == 0) ? COL_HEAD : COL_BODY;
            o->visible = true;
            world->update_world_aabb(*o);
        } else {
            o->visible = false;
        }
    }
}

void SnakeApp::step_snake() {
    if (!running || paused || game_over) return;

    // Apply buffered direction (prevents double-turn in one tick)
    dir_x = pending_dx;
    dir_y = pending_dy;

    int nx = gx[0] + dir_x;
    int ny = gy[0] + dir_y;

    // Wall death
    if (nx < 0 || ny < 0 || nx >= GRID_W || ny >= GRID_H) {
        game_over = true;
        running = false;
        if (!scored_this_round) {
            scored_this_round = true;
            game_stats::record_round(kStatsName, stats, (uint32_t)score);
        }
        update_hud();
        ESP_LOGI(TAG, "Game over (wall) score=%d", score);
        return;
    }

    // Self collision
    for (int i = 0; i < length; ++i) {
        if (gx[i] == nx && gy[i] == ny) {
            game_over = true;
            running = false;
            if (!scored_this_round) {
                scored_this_round = true;
                game_stats::record_round(kStatsName, stats, (uint32_t)score);
            }
            update_hud();
            ESP_LOGI(TAG, "Game over (self) score=%d", score);
            return;
        }
    }

    bool ate = (nx == food_x && ny == food_y);

    // Shift body
    for (int i = length - 1; i > 0; --i) {
        gx[i] = gx[i - 1];
        gy[i] = gy[i - 1];
    }
    gx[0] = (int8_t)nx;
    gy[0] = (int8_t)ny;

    if (ate) {
        if (length < MAX_LEN) {
            gx[length] = gx[length - 1];
            gy[length] = gy[length - 1];
            length++;
        }
        score += 10;
        // Speed up a bit
        if (step_ms > 80) step_ms -= 4;
        spawn_food();
        ESP_LOGI(TAG, "Ate! len=%d score=%d step=%u", length, score, (unsigned)step_ms);
    }

    sync_sprites();
    update_hud();
}

void SnakeApp::update_hud() {
    if (!win) return;
    char buf[192];
    if (game_over) {
        snprintf(buf, sizeof(buf),
                 "<|size=2|><|color=0xF800|>GAME OVER  <|color=0xFFFF|>%d"
                 "  <|color=0x8888|>HI %u<|n|>"
                 "<|size=1|><|color=0x8888|>HOLD-ENTER=retry  BACK=menu",
                 score, (unsigned)stats.high_score);
    } else if (paused) {
        snprintf(buf, sizeof(buf),
                 "<|size=2|>SNAKE  %d  HI %u  <|color=0xFDFC|>PAUSED<|n|>"
                 "<|size=1|><|color=0x8888|>ENTER=resume  D-pad=steer",
                 score, (unsigned)stats.high_score);
    } else {
        snprintf(buf, sizeof(buf),
                 "<|size=2|>SNAKE  <|color=0x07E0|>%d  <|color=0x8888|>HI %u<|n|>"
                 "<|size=1|><|color=0x8888|>D-pad=move  ENTER=pause  BACK=menu",
                 score, (unsigned)stats.high_score);
    }
    win->SetText(buf);
}

// ---------------------------------------------------------------------------

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
            if (dir_y == 0) { pending_dx = 0; pending_dy = -1; }
            break;
        case KEY_DOWN:
            if (dir_y == 0) { pending_dx = 0; pending_dy = 1; }
            break;
        case KEY_LEFT:
            if (dir_x == 0) { pending_dx = -1; pending_dy = 0; }
            break;
        case KEY_RIGHT:
            if (dir_x == 0) { pending_dx = 1; pending_dy = 0; }
            break;
        case KEY_ENTER:
            if (game_over) {
                reset_game();
            } else {
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
    m.description = "Classic snake on AnimWorld grid";
    m.capabilities = static_cast<uint32_t>(AppCapability::FULLSCREEN) |
                     static_cast<uint32_t>(AppCapability::NEEDS_WINDOW);
    m.stack_size_bytes = 10240;
    m.priority = 5;
    m.tick_rate_hz = 30;
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<SnakeApp>(cfg);
    };
    appManager::instance().register_app(m);
}
