#include "touch_interact.h"
#include "esp_timer.h"
#include "os_code/core/window_env/MWenv.hpp"

static int16_t px = -32, py = -32;
static int visible = 0;
static uint32_t last_ms = 0;

static ui_widget_t* first = nullptr;
static uint32_t first_ms = 0;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void touch_interact_init(void)
{
    px = -32; py = -32; visible = 0;
    first = nullptr; first_ms = 0; last_ms = 0;
}

void touch_interact_point(int16_t x, int16_t y, int down)
{
    if (!down) return;
    uint32_t t = now_ms();
    last_ms = t;
    visible = 1;
    px = x; py = y;

    std::shared_ptr<Window> win;
    ui_widget_t* el = WindowManager::getInstance().hit_element(x, y, &win);

    /* double tap on the same element */
    if (first && (t - first_ms) <= 500 && el && el == first) {
        if (win && win->window_highlighted) {
            el->focused = true;          /* select on the focused panel */
        } else if (win) {
            WindowManager::getInstance().raise_to_front(win);
        }
        first = nullptr;
        return;
    }

    /* single click: just reposition. non-focused panel comes forward */
    if (win && !win->window_highlighted) {
        WindowManager::getInstance().raise_to_front(win);
    }
    first = el;
    first_ms = t;
}

void touch_interact_pointer(int16_t* x, int16_t* y, int* vis)
{
    uint32_t t = now_ms();
    if (visible && last_ms && (t - last_ms) > 8000) visible = 0;
    if (first && (t - first_ms) > 500) first = nullptr;
    if (x) *x = px;
    if (y) *y = py;
    if (vis) *vis = visible;
}
