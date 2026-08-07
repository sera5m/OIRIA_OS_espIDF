#pragma once

#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"

#include "html_to_mwenv.hpp"

// ---------------------------------------------------------------------------
// BrowserApp – minimal HTML viewer on MWenv
//
// Not a full web engine. It:
//   1) Loads a page (built-in demo set, about:, or future http://)
//   2) Runs html_to_mwenv → MWenv markup
//   3) Shows a viewport of lines in a Window (scroll with UP/DOWN)
//
// Controls:
//   UP / DOWN     – scroll
//   LEFT / RIGHT  – history back / forward
//   ENTER         – follow selected link (or open link picker mode)
//   HOLD-ENTER    – go home
//   BACK          – menu
// ---------------------------------------------------------------------------

class BrowserApp : public AppBase {
public:
    explicit BrowserApp(const ApplicationConfig& cfg);

    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void on_draw() override;

    void on_start() override;
    void on_stop() override;
    void on_pause() override;
    void on_resume() override;

private:
    std::shared_ptr<Window> win;

    // Current document
    std::string url;
    HtmlDoc     doc;
    std::vector<std::string> lines;   // markup split on <|n|>
    int scroll = 0;                   // first visible line
    int visible_lines = 12;

    // Link focus (0 = none, 1..N = doc.links index)
    int selected_link = 0;

    // History
    std::vector<std::string> history;
    int hist_pos = -1;

    void navigate(const std::string& target, bool push_hist = true);
    void load_builtin(const std::string& name);
    void apply_doc();
    void rebuild_viewport();
    void update_chrome();

    // Built-in pages (no network required)
    static const char* page_home();
    static const char* page_about();
    static const char* page_kernel();
    static const char* page_help();
};

void register_browser();
