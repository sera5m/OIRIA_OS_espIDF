#include "MS_browserapp.hpp"

#include <stdio.h>
#include <string.h>
#include <algorithm>

// Optional: real WiFi status when the component is linked
#if __has_include("esp_wifi.h")
#include "esp_wifi.h"
#include "esp_netif.h"
#define BROWSER_HAS_WIFI 1
#else
#define BROWSER_HAS_WIFI 0
#endif

static const char* TAG = "BrowserApp";

static constexpr uint16_t COL_BG = 0x10A2;

// ---------------------------------------------------------------------------
// Built-in HTML pages (subset understood by html_to_mwenv)
// ---------------------------------------------------------------------------

const char* BrowserApp::page_wifi() {
    return R"HTML(<html><head><title>WiFi</title></head><body>
<h1>WiFi required</h1>
<p>HTTP pages need a network. Connect WiFi from
<b>Wireless</b> in the main menu, then return here.</p>
<p>Built-in pages work offline:</p>
<ul>
  <li><a href="about:home">Home</a></li>
  <li><a href="about:kernel">Kernel</a></li>
  <li><a href="about:help">Help</a></li>
</ul>
</body></html>)HTML";
}

const char* BrowserApp::page_home() {
    return R"HTML(<!DOCTYPE html>
<html><head><title>Home</title></head>
<body>
<h1>rShell Browser</h1>
<p>Constrained HTML viewer. Pages convert via <code>html_to_mwenv</code>.</p>
<hr>
<h2>Pages</h2>
<ul>
  <li><a href="about:home">Home</a></li>
  <li><a href="about:kernel">Kernel notes</a></li>
  <li><a href="about:help">Help</a></li>
  <li><a href="about:about">About</a></li>
  <li><a href="about:wifi">WiFi status</a></li>
</ul>
<p style="color:#888">http:// needs WiFi. Offline: about: pages only.</p>
</body></html>)HTML";
}

const char* BrowserApp::page_about() {
    return R"HTML(<html><head><title>About</title></head><body>
<h1>About</h1>
<p>This is <b>not</b> Chromium. It maps a <i>small HTML subset</i> onto the
existing window tokenizer:</p>
<ul>
  <li>headings, paragraphs, lists</li>
  <li>bold / italic / underline</li>
  <li>links (collect + follow)</li>
  <li>basic colors via style or color=</li>
</ul>
<p>Scripts, CSS layouts, images, and real JS are stripped.</p>
<p><a href="about:home">Back home</a></p>
</body></html>)HTML";
}

const char* BrowserApp::page_help() {
    return R"HTML(<html><head><title>Help</title></head><body>
<h2>Controls</h2>
<ul>
  <li><b>UP / DOWN</b> — scroll</li>
  <li><b>LEFT</b> — history back</li>
  <li><b>RIGHT</b> — history forward</li>
  <li><b>ENTER</b> — cycle / open link</li>
  <li><b>HOLD-ENTER</b> — home</li>
  <li><b>BACK</b> — menu</li>
</ul>
<h3>URLs</h3>
<p><code>about:home</code> <code>about:kernel</code>
<code>about:help</code> <code>about:about</code></p>
<p><a href="about:home">Home</a></p>
</body></html>)HTML";
}

const char* BrowserApp::page_kernel() {
    // Condensed HTML version of KERNEL_NOTES themes
    return R"HTML(<html><head><title>Kernel</title></head><body>
<h1>rShell Kernel</h1>
<p>Cooperative app runtime on <b>FreeRTOS + ESP-IDF</b>. Not a Unix kernel.</p>
<h2>Layers</h2>
<ol>
  <li>Apps (Watch, Pong, Snake, 2048, Browser…)</li>
  <li>appManager — registry, focus, input</li>
  <li>WindowManager + MWenv + Canvas + AnimWorld</li>
  <li>Streams / Pipes / DataPool</li>
  <li>Drivers, ULP, SD, LCD</li>
</ol>
<h2>Apps</h2>
<p>Each app is a task. <code>AppManifest</code> supplies name, stack,
tick rate, capabilities, and a factory. Focus owns input via
<code>route_input_to_focused</code>.</p>
<h2>Drawing</h2>
<p><code>WinDraw</code>: background → rich text → <b>DrawCanvas</b> → border.
Canvas must paint <i>after</i> the fill or sprites are wiped.</p>
<h2>DataPool</h2>
<p>Software MMU: RAM/PSRAM buffers with READ_ONLY / READ_WRITE / EXCLUSIVE
tokens. Optional ring mode. <code>.rpool</code> on SD for snapshots.
Games store high scores under
<code>/sdcard/apps/savedat/*_gamestats.rgs</code>.</p>
<h2>Streams</h2>
<p><code>DataItem</code> units move through the pump task into pipes
(direct / fan / clone) toward apps or pools.</p>
<p><a href="about:home">Home</a> · <a href="about:help">Help</a></p>
</body></html>)HTML";
}

// ---------------------------------------------------------------------------

BrowserApp::BrowserApp(const ApplicationConfig& cfg) : AppBase(cfg) {
    appTickRateHZ = 15;
}

void BrowserApp::refresh_wifi_status() {
    wifi_ok = false;
#if BROWSER_HAS_WIFI
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        wifi_ok = true;
    }
#endif
}

void BrowserApp::on_start() {
    ESP_LOGI(TAG, "Browser starting");

    win = std::make_shared<Window>(
        WindowCfg{
            .Posx = 0, .Posy = 0,
            .Layer = 0, .renderPriority = 0,
            .win_width  = 280,
            .win_height = 240,
            .win_rotation = 1,
            .AutoAlignment = false,
            .WrapText = true,
            .borderless = false,
            .ShowNameAtTopOfWindow = false,
            .TextSizeMult = 1,
            .BorderColor = 0x4A69,
            .BgColor = COL_BG,
            .Bg_secondaryColor = 0x2104,
            .WinTextColor = 0xFFFF,
            .backgroundType = BgFillType::Solid,
            .UpdateRate = 0.25f
        },
        "Browser"
    );

    WindowManager::getInstance().registerWindow(win);
    bind_main_window(win);
    WindowManager::getInstance().make_window_fullscreen(win);
    WindowManager::getInstance().SetToolbarActive(false);

    // Keep redrawing so the page does not vanish under other WM activity
    win->enable_refresh_override = true;
    win->dirty = true;

    history.clear();
    hist_pos = -1;
    refresh_wifi_status();
    // Land on WiFi help first when offline so the prompt is impossible to miss
    if (!wifi_ok) {
        navigate("about:wifi", true);
    } else {
        navigate("about:home", true);
    }
    // Force a full WM pass so the first frame is not blank
    WindowManager::getInstance().UpdateAll(true, true, false, false);
}

void BrowserApp::on_stop() {
    if (win) {
        win->enable_refresh_override = false;
        WindowManager::getInstance().restore_from_fullscreen();
        WindowManager::getInstance().unregisterWindow(win);
        win.reset();
    }
}

void BrowserApp::on_pause()  {}
void BrowserApp::on_resume() {
    refresh_wifi_status();
    if (win) {
        win->dirty = true;
        rebuild_viewport();
    }
}

// ---------------------------------------------------------------------------

void BrowserApp::load_builtin(const std::string& name) {
    const char* html = page_home();
    if (name == "home" || name == "" || name == "index") html = page_home();
    else if (name == "about")  html = page_about();
    else if (name == "help")   html = page_help();
    else if (name == "kernel") html = page_kernel();
    else if (name == "wifi")   html = page_wifi();
    else {
        // unknown about: → simple error page
        static char err[512];
        snprintf(err, sizeof(err),
                 "<html><head><title>Not found</title></head><body>"
                 "<h2>Not found</h2><p>No page <code>%s</code>.</p>"
                 "<p><a href=\"about:home\">Home</a></p></body></html>",
                 name.c_str());
        HtmlConvertOptions opt;
        opt.base_size = 1;
        doc = html_to_mwenv(err, strlen(err), opt);
        return;
    }
    HtmlConvertOptions opt;
    opt.base_size = 1;
    opt.link_color = 0x5D7F;
    doc = html_to_mwenv(html, strlen(html), opt);
}

void BrowserApp::navigate(const std::string& target, bool push_hist) {
    url = target;
    selected_link = 0;
    scroll = 0;

    ESP_LOGI(TAG, "Navigate: %s", url.c_str());

    if (url.rfind("about:", 0) == 0) {
        load_builtin(url.substr(6));
    } else if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        refresh_wifi_status();
        if (!wifi_ok) {
            load_builtin("wifi");
            url = "about:wifi";
        } else {
            // Stub until esp_http_client is hooked
            const char* stub =
                "<html><head><title>HTTP</title></head><body>"
                "<h2>HTTP not wired yet</h2>"
                "<p>WiFi is up. Hook <code>esp_http_client</code> → PSRAM → "
                "<code>html_to_mwenv</code>.</p>"
                "<p><a href=\"about:home\">Home</a></p>"
                "</body></html>";
            doc = html_to_mwenv(stub, strlen(stub));
        }
    } else {
        // Treat bare words as about:
        load_builtin(url);
        url = "about:" + target;
    }

    if (!doc.ok) {
        ESP_LOGW(TAG, "Convert failed: %s", doc.error.c_str());
        doc.markup = "<|size=2|><|color=0xF800|>Convert error<|n|>";
        doc.title = "Error";
    }

    if (push_hist) {
        // Drop forward tail
        if (hist_pos + 1 < (int)history.size())
            history.resize(hist_pos + 1);
        history.push_back(url);
        hist_pos = (int)history.size() - 1;
    }

    apply_doc();
}

void BrowserApp::apply_doc() {
    // Split markup into lines on <|n|> for scroll windowing
    lines.clear();
    const std::string& m = doc.markup;
    size_t i = 0;
    while (i < m.size()) {
        size_t j = m.find("<|n|>", i);
        if (j == std::string::npos) {
            lines.push_back(m.substr(i));
            break;
        }
        lines.push_back(m.substr(i, j - i));
        i = j + 5;
    }
    if (lines.empty()) lines.push_back("");
    rebuild_viewport();
}

void BrowserApp::rebuild_viewport() {
    if (!win) return;
    if (scroll < 0) scroll = 0;
    int max_s = std::max(0, (int)lines.size() - visible_lines);
    if (scroll > max_s) scroll = max_s;

    // Chrome + visible slice
    std::string body;
    body.reserve(2048);

    // Title bar + WiFi banner
    char chrome[220];
    refresh_wifi_status();
    snprintf(chrome, sizeof(chrome),
             "<|size=1|><|color=0x07FF|>%s  <|color=%s|>[%s]<|n|>"
             "<|color=0x8410|>%s<|n|>"
             "<|color=0x5A6B|>---------------------------<|n|>",
             doc.title.c_str(),
             wifi_ok ? "0x07E0" : "0xF800",
             wifi_ok ? "WiFi OK" : "No WiFi",
             url.c_str());
    body += chrome;
    if (!wifi_ok && show_wifi_banner) {
        body += "<|color=0xF800|>Connect WiFi in Wireless app for http://<|n|>";
    }

    // Content window
    int end = std::min((int)lines.size(), scroll + visible_lines);
    for (int li = scroll; li < end; ++li) {
        body += lines[li];
        body += "<|n|>";
    }

    // Footer: scroll pos + link hint
    body += "<|color=0x5A6B|>---------------------------<|n|>";
    char foot[128];
    if (!doc.links.empty()) {
        int idx = selected_link;
        if (idx < 1 || idx > (int)doc.links.size()) idx = 1;
        const auto& lk = doc.links[idx - 1];
        snprintf(foot, sizeof(foot),
                 "<|size=1|><|color=0xAD55|>%d/%d  link[%d] %s",
                 scroll + 1, std::max(1, (int)lines.size()),
                 idx, lk.href.c_str());
    } else {
        snprintf(foot, sizeof(foot),
                 "<|size=1|><|color=0xAD55|>%d/%d  (no links)",
                 scroll + 1, std::max(1, (int)lines.size()));
    }
    body += foot;

    win->SetText(body.c_str());
    win->dirty = true;
}

void BrowserApp::update_chrome() {
    rebuild_viewport();
}

// ---------------------------------------------------------------------------

void BrowserApp::tick_app(uint32_t delta_ms) {
    (void)delta_ms;
    if (win) win->dirty = true;
}

void BrowserApp::on_draw() {
    if (win) win->dirty = true;
}

void BrowserApp::receive_event_input(const void* event) {
    if (!event) return;
    const InputEvent* ev = static_cast<const InputEvent*>(event);

    if (ev->action == KeyAction::Hold && ev->key == KEY_ENTER) {
        navigate("about:home", true);
        return;
    }

    if (ev->action != KeyAction::Tap && ev->action != KeyAction::Hold)
        return;

    switch (ev->key) {
        case KEY_UP:
            scroll -= (ev->action == KeyAction::Hold) ? 3 : 1;
            rebuild_viewport();
            break;
        case KEY_DOWN:
            scroll += (ev->action == KeyAction::Hold) ? 3 : 1;
            rebuild_viewport();
            break;
        case KEY_LEFT:
            if (hist_pos > 0) {
                --hist_pos;
                navigate(history[hist_pos], false);
            }
            break;
        case KEY_RIGHT:
            if (hist_pos + 1 < (int)history.size()) {
                ++hist_pos;
                navigate(history[hist_pos], false);
            }
            break;
        case KEY_ENTER:
            if (doc.links.empty()) break;
            // ENTER: open currently selected link, then advance selection for next time.
            if (selected_link < 1 || selected_link > (int)doc.links.size())
                selected_link = 1;
            {
                std::string href = doc.links[selected_link - 1].href;
                selected_link++;
                if (selected_link > (int)doc.links.size()) selected_link = 1;
                if (!href.empty()) navigate(href, true);
            }
            break;
        case KEY_BACK:
            appManager::instance().close_current_and_open("MenuApp");
            break;
        default:
            break;
    }
}

void register_browser() {
    AppManifest m;
    m.name = "BrowserApp";
    m.display_name = "Browser";
    m.description = "Constrained HTML viewer (html_to_mwenv → MWenv)";
    m.capabilities = static_cast<uint32_t>(AppCapability::FULLSCREEN) |
                     static_cast<uint32_t>(AppCapability::NEEDS_WINDOW);
    m.stack_size_bytes = 32768;  // html_to_mwenv + viewport strings
    m.priority = 5;
    m.tick_rate_hz = 15;
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<BrowserApp>(cfg);
    };
    appManager::instance().register_app(m);
}
