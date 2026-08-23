// VulcanApp – RS-VM on-watch test console (aligned with real MWenv / AppBase APIs)
#include "MS_vulcanapp.hpp"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static const char* TAG = "VulcanApp";

static const char* kEmbeddedSample =
    "set_step_depth 50000;\n"
    "string phrase = \"meow\";\n"
    "fn drawCat in[] out[] {\n"
    "  print(\"/\\\\_/\\\\\");\n"
    "  print(\"| o.o |\");\n"
    "  print(\"> ^ <\");\n"
    "  return;\n"
    "}\n"
    "fn main in[] out[] {\n"
    "  i32 plen = strlen(phrase);\n"
    "  i32 endAt = 6;\n"
    "  i32 idx = 0;\n"
    "  do n[endAt] {\n"
    "    i32 lim = idx + 1;\n"
    "    i32 j = 0;\n"
    "    do n[lim] {\n"
    "      printc(phrase[j % plen]);\n"
    "      j = j + 1;\n"
    "    }\n"
    "    printnl();\n"
    "    idx = idx + 1;\n"
    "  }\n"
    "  drawCat in[];\n"
    "  return;\n"
    "}@monitor_time_highres @monitor_execs;\n";

// ---------------------------------------------------------------------------
// Host UI sink — prints go into the app log buffer
// ---------------------------------------------------------------------------
static void ui_print_i32(int32_t v, void* user) {
    auto* app = static_cast<VulcanApp*>(user);
    if (!app) return;
    char tmp[24];
    int n = snprintf(tmp, sizeof tmp, "%ld\n", (long)v);
    app->append_log(tmp, n);
}
static void ui_print_str(const char* s, uint8_t len, void* user) {
    auto* app = static_cast<VulcanApp*>(user);
    if (!app || !s) return;
    app->append_log(s, (int)len);
    app->append_char('\n');
}
static void ui_print_char(char c, void* user) {
    auto* app = static_cast<VulcanApp*>(user);
    if (app) app->append_char(c);
}
static void ui_delay_ms(uint32_t ms, void*) {
    vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}

void rsvm_install_esp_host_ui(rsvm_t* vm, VulcanApp* app) {
    if (!vm) return;
    rsvm_host_t h{};
    h.print_i32  = ui_print_i32;
    h.print_str  = ui_print_str;
    h.print_char = ui_print_char;
    h.delay_ms   = ui_delay_ms;
    h.user       = app;
    rsvm_set_host(vm, &h);
}

// ---------------------------------------------------------------------------
// App lifecycle
// ---------------------------------------------------------------------------
VulcanApp::VulcanApp(const ApplicationConfig& cfg) : AppBase(cfg) {
    appTickRateHZ = 10;
}

void VulcanApp::on_start() {
    ESP_LOGI(TAG, "Vulcan starting");
    clear_log();
    ran_once = false;
    vm_ready = false;

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
            .ShowNameAtTopOfWindow = true,
            .TextSizeMult = 1,
            .BorderColor = 0x07E0,
            .BgColor = 0x0000,
            .Bg_secondaryColor = 0x0000,
            .WinTextColor = 0x07E0,
            .backgroundType = BgFillType::Solid,
            .UpdateRate = 0.5f
        },
        "Vulcan VM"
    );
    // name for title bar
    strncpy(win->Initialcfg.name, "Vulcan", sizeof win->Initialcfg.name - 1);

    WindowManager::getInstance().registerWindow(win);
    bind_main_window(win);

    snprintf(status_line, sizeof status_line, "ENTER=run  BACK=exit");
    redraw_text();
}

void VulcanApp::on_stop() {
    ESP_LOGI(TAG, "Vulcan stop");
    if (win) {
        WindowManager::getInstance().unregisterWindow(win);
        win.reset();
    }
    vm_ready = false;
}

void VulcanApp::on_pause()  { /* keep log */ }
void VulcanApp::on_resume() { redraw_text(); }

void VulcanApp::tick_app(uint32_t) {
    // one-shot auto-run after first frames
    if (!ran_once && win) {
        ran_once = true;
        run_embedded_sample();
    }
}

void VulcanApp::on_draw() {
    redraw_text();
    if (win) win->dirty = true;
}

// ---------------------------------------------------------------------------
// Log buffer
// ---------------------------------------------------------------------------
void VulcanApp::clear_log() {
    log_len = 0;
    log_buf[0] = 0;
    scroll = 0;
}

void VulcanApp::append_log(const char* s, int len) {
    if (!s || len <= 0) return;
    if (log_len + len >= LOG_CAP) {
        // drop from front
        int drop = (log_len + len) - LOG_CAP + 64;
        if (drop > log_len) drop = log_len;
        memmove(log_buf, log_buf + drop, (size_t)(log_len - drop));
        log_len -= drop;
    }
    if (len > LOG_CAP - 1 - log_len) len = LOG_CAP - 1 - log_len;
    memcpy(log_buf + log_len, s, (size_t)len);
    log_len += len;
    log_buf[log_len] = 0;
}

void VulcanApp::append_char(char c) {
    append_log(&c, 1);
}

// ---------------------------------------------------------------------------
// Run sample
// ---------------------------------------------------------------------------
void VulcanApp::run_embedded_sample() {
    clear_log();
    append_log("--- vulcan run ---\n", 18);

    rsvm_init(&vm);
    rsvm_install_esp_host_ui(&vm, this);

    rsvm_parse_err_t err{};
    rsvm_status_t st = rsvm_eval(&vm, kEmbeddedSample, &err);
    vm_ready = (st == RSVM_OK);

    if (st != RSVM_OK) {
        char line[96];
        snprintf(line, sizeof line, "ERR L%d:%d %s\n", err.line, err.column, err.message);
        append_log(line, (int)strlen(line));
        snprintf(status_line, sizeof status_line, "error");
    } else {
        char line[64];
        snprintf(line, sizeof line, "OK steps=%" PRIu32 "\n", vm.steps);
        append_log(line, (int)strlen(line));
        snprintf(status_line, sizeof status_line, "OK steps=%" PRIu32, vm.steps);
    }
    redraw_text();
}

void VulcanApp::redraw_text() {
    if (!win) return;

    // Build a single string for SetText (MWenv owns layout/draw)
    char screen[1200];
    int o = 0;
    auto put = [&](const char* s) {
        int n = (int)strlen(s);
        if (o + n >= (int)sizeof(screen) - 1) return;
        memcpy(screen + o, s, (size_t)n);
        o += n;
    };

    put("Vulcan VM\n");
    put(status_line);
    put("\n----\n");

    // last portion of log (simple tail)
    const char* start = log_buf;
    int lines = 0;
    for (int i = log_len - 1; i >= 0; --i) {
        if (log_buf[i] == '\n') {
            lines++;
            if (lines > MAX_LINES_VISIBLE) {
                start = log_buf + i + 1;
                break;
            }
        }
    }
    put(start);
    screen[o] = 0;

    win->SetText(screen);
    win->dirty = true;
}

// ---------------------------------------------------------------------------
// Input — KEY_ENTER re-run, KEY_BACK close
// ---------------------------------------------------------------------------
void VulcanApp::receive_event_input(const void* event) {
    if (!event) return;
    const InputEvent* ev = static_cast<const InputEvent*>(event);
    if (ev->action != KeyAction::Tap) return;

    switch (ev->key) {
    case KEY_BACK:
        appManager::instance().close_current_and_open("MenuApp");
        break;
    case KEY_ENTER:
        run_embedded_sample();
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Registration (same pattern as PongApp)
// ---------------------------------------------------------------------------
void register_vulcan() {
    AppManifest m;
    m.name = "VulcanApp";
    m.display_name = "Vulcan";
    m.description = "RS-VM / Vulcan script console";
    m.capabilities = static_cast<uint32_t>(AppCapability::NEEDS_WINDOW);
    m.stack_size_bytes = 16384;
    m.priority = 5;
    m.tick_rate_hz = 10;
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<VulcanApp>(cfg);
    };
    appManager::instance().register_app(m);
}