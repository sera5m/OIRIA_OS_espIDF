#pragma once
/*
 * On-screen keyboard. Privileged system app.
 * Occupies one half of the screen, split on the long axis:
 *   portrait:  x = 0 .. width/2,  y = 0 .. height
 *   landscape: the same rect, rotated with the window.
 * Claims hot focus. Zone hits become keys. Raw XY is intercepted.
 */
#include <memory>
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/core/window_env/Canvas.hpp"

class KeyboardApp : public AppBase {
public:
    explicit KeyboardApp(const ApplicationConfig& cfg);
    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void on_start() override;
    void on_stop() override;

    /* Open and claim hot focus. Close and release it. */
    static void show();
    static void hide();

private:
    std::shared_ptr<Window> win;
    std::shared_ptr<Canvas> canvas;
    uint8_t last_rot = 0xFF;

    void layout(uint8_t rot);
    void place_key(int x, int y, int w, int h, uint16_t key);
};

void register_keyboard();
