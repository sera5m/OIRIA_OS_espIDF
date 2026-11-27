#include "keyboard.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "os_code/middle_layer/input/hid_t.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "esp_log.h"

static const char* TAG = "keyboard";
static std::shared_ptr<KeyboardApp> g_kb;

/* Simplified rows. Each entry is a key code. */
static const uint16_t row0[] = { 'q','w','e','r','t','y','u','i','o','p' };
static const uint16_t row1[] = { 'a','s','d','f','g','h','j','k','l' };
static const uint16_t row2[] = { 'z','x','c','v','b','n','m', KEY_BACK };
static const uint16_t row3[] = { ' ', KEY_ENTER };

KeyboardApp::KeyboardApp(const ApplicationConfig& cfg) : AppBase(cfg) {}

void KeyboardApp::on_start()
{
    int half = SCREEN_W / 2;
    win = std::make_shared<Window>("keyboard", 0, 0, half, SCREEN_H);
    win->claim_os_layer();
    WindowManager::getInstance().registerWindow(win);

    CanvasCfg cfg;
    cfg.x = 0; cfg.y = 0; cfg.width = half; cfg.height = SCREEN_H;
    cfg.bgColor = 0x1082;
    cfg.parentWindow = win.get();
    canvas = win->AddCanvas(cfg);

    layout(0);
    appManager::instance().set_hot_focus(shared_from_this());
    ESP_LOGI(TAG, "keyboard up");
}

void KeyboardApp::on_stop()
{
    appManager::instance().clear_hot_focus();
    if (win) {
        WindowManager::getInstance().unregisterWindow(win);
        win.reset();
    }
    canvas.reset();
}

void KeyboardApp::place_key(int x, int y, int w, int h, uint16_t key)
{
    InputZone z{};
    z.bounds = { (int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h };
    z.kind = ZoneKind::Key;
    z.key = key;
    canvas->AddZone(z);
}

void KeyboardApp::layout(uint8_t rot)
{
    if (!canvas) return;
    canvas->ClearZones();
    last_rot = rot;

    /* Half the long axis. Portrait: width/2 wide, full height.
       On rotation the window rect is swapped by the compositor;
       zones stay in canvas-local space. */
    int W = canvas->GetWidth();
    int H = canvas->GetHeight();
    int rows = 4;
    int rh = H / rows;
    int gap = 2;

    auto row = [&](const uint16_t* keys, int n, int y) {
        int cw = (W - gap) / n;
        for (int i = 0; i < n; ++i)
            place_key(i * cw + gap, y + gap, cw - gap, rh - gap, keys[i]);
    };
    row(row0, 10, 0);
    row(row1, 9, rh);
    row(row2, 8, rh * 2);
    row(row3, 2, rh * 3);
}

void KeyboardApp::tick_app(uint32_t)
{
    uint8_t rot = win ? win->Currentcfg.win_rotation : 0;
    if (rot != last_rot) layout(rot);
}

void KeyboardApp::receive_event_input(const void*) {}

void KeyboardApp::show()
{
    if (g_kb) return;
    ApplicationConfig cfg{};
    g_kb = std::make_shared<KeyboardApp>(cfg);
    g_kb->on_start();
}

void KeyboardApp::hide()
{
    if (!g_kb) return;
    g_kb->on_stop();
    g_kb.reset();
}

void register_keyboard()
{
    /* Opened on demand via KeyboardApp::show(), not from the menu. */
}
