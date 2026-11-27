#include "touch_interact.h"
#include "esp_timer.h"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/core/window_env/Canvas.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"

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

    /* Zone on the window's canvas maps to a key, mouse delta, or macro. */
    if (win && win->GetCanvas()) {
        int ox = 0, oy = 0, ww = 0, wh = 0;
        win->get_physical_bounds(ox, oy, ww, wh);
        int zi = win->GetCanvas()->ZoneAt(x - ox, y - oy);
        if (zi >= 0) {
            const InputZone* z = win->GetCanvas()->GetZone(zi);
            if (z) {
                InputEvent ev{};
                ev.timestamp = t;
                ev.source_device_type = HIDInputDeviceType::Mouse;
                if (z->kind == ZoneKind::Key) {
                    ev.key = z->key;
                    ev.action = KeyAction::Tap;
                } else if (z->kind == ZoneKind::Mouse) {
                    ev.action = KeyAction::PositionDelta;
                    ev.delta = z->dx;
                } else {
                    ev.key = z->macro_n ? z->macro[0] : 0;
                    ev.action = KeyAction::Tap;
                }
                if (ProcInputQueTarget)
                    xQueueSend(ProcInputQueTarget, &ev, 0);
                /* On-screen HID owns this point. Converted event goes to
                   the target focus. Raw XY is discarded. */
                if (appManager::instance().is_using_onscreen_hid()) {
                    appManager::instance().route_input_to_focused(ev);
                    first = el;
                    first_ms = t;
                    return;
                }
            }
        }
    }

    /* Raw pointer path. Suppressed while an on-screen HID is active. */
    if (appManager::instance().is_using_onscreen_hid()) {
        first = el;
        first_ms = t;
        return;
    }

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
