// VulcanApp – RS-VM on-watch test console
#include "MS_vulcanapp.hpp"

#include <stdio.h>
#include <string.h>

static const char* TAG = "VulcanApp";

// ---------------------------------------------------------------------------
// Embedded sample (no SD required) — short progressive + cat
// ---------------------------------------------------------------------------
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
    "  drawCat();\n"
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

void VulcanApp::clear_log() {
    log_len = 0;
    log_buf[0] = 0;
    scroll = 0;
}

void VulcanApp::append_log(const char* s, int len) {
    if (!s || len <= 0) return;
    if (len > LOG_CAP - 1) len = LOG_CAP - 1;
    // ring-ish: if full, drop from front
    if (log_len + len >= LOG_CAP - 1) {
        int drop = (log_len + len) - (LOG_CAP - 1);
        if (drop > log_len) drop = log_len;
        memmove(log_buf, log_buf + drop, (size_t)(log_len - drop));
        log_len -= drop;
    }
    memcpy(log_buf + log_len, s, (size_t)len);
    log_len += len;
    log_buf[log_len] = 0;
}

void VulcanApp::append_char(char c) {
    append_log(&c, 1);
}

void VulcanApp::on_start() {
    ESP_LOGI(TAG, "on_start");
    clear_log();
    ran_once = false;
    vm_ready = false;

    // Full-screen-ish text window
    win = WindowManager::instance().create_window(
        "Vulcan", /*x*/0, /*y*/0, /*w*/240, /*h*/240);
    if (win) {
        win->set_bg(0x0000); // black
        win->clear();
    }

    rsvm_init(&vm);
    rsvm_install_esp_host_ui(&vm, this);
    rsvm_register_modules(&vm);
    vm_ready = true;

    snprintf(status_line, sizeof status_line, "ENTER=run  BACK=exit");
    append_log("RS-VM watch console\n", 20);
    append_log(status_line, (int)strlen(status_line));
    append_char('\n');
    redraw_text();
}

void VulcanApp::on_stop() {
    ESP_LOGI(TAG, "on_stop");
    vm_ready = false;
    if (win) {
        WindowManager::instance().destroy_window(win);
        win.reset();
    }
}

void VulcanApp::on_pause()  {}
void VulcanApp::on_resume() { redraw_text(); }

void VulcanApp::run_embedded_sample() {
    if (!vm_ready) return;
    clear_log();
    append_log("running sample...\n", 18);

    rsvm_parse_err_t err{};
    rsvm_status_t st = rsvm_eval(&vm, kEmbeddedSample, &err);

    char line[96];
    if (st != RSVM_OK) {
        snprintf(line, sizeof line, "ERR %s L%d:%d %s\n",
                 rsvm_status_str(st), err.line, err.column, err.message);
        append_log(line, (int)strlen(line));
        ESP_LOGE(TAG, "%s", line);
    } else {
        snprintf(line, sizeof line, "OK steps=%u us~monitor\n", vm.steps);
        append_log(line, (int)strlen(line));
    }
    ran_once = true;
    redraw_text();
}

void VulcanApp::redraw_text() {
    if (!win) return;
    win->clear();

    // Title
    win->draw_text(4, 4, "Vulcan VM", 0xFFFF);

    // Status
    win->draw_text(4, 18, status_line, 0x07FF);

    // Log: show last MAX_LINES_VISIBLE lines
    // Split log_buf by \n from the end
    const char* lines[64];
    int nlines = 0;
    {
        // temporary scan
        static char tmp[LOG_CAP];
        memcpy(tmp, log_buf, (size_t)log_len);
        tmp[log_len] = 0;
        char* save = nullptr;
        char* tok = strtok_r(tmp, "\n", &save);
        while (tok && nlines < 64) {
            lines[nlines++] = tok;
            tok = strtok_r(nullptr, "\n", &save);
        }
    }

    int start = nlines - MAX_LINES_VISIBLE - scroll;
    if (start < 0) start = 0;
    int y = 36;
    for (int i = start; i < nlines && y < 230; ++i) {
        win->draw_text(4, y, lines[i], 0x07E0); // green terminal vibe
        y += LINE_H;
    }
}

void VulcanApp::tick_app(uint32_t /*delta_ms*/) {
    // Auto-run once a moment after start so the user sees output immediately
    if (vm_ready && !ran_once) {
        run_embedded_sample();
    }
}

void VulcanApp::on_draw() {
    // Text is redrawn on demand; keep light
}

void VulcanApp::receive_event_input(const void* event) {
    if (!event) return;
    const InputEvent* ev = static_cast<const InputEvent*>(event);

    if (ev->action == KeyAction::Tap || ev->action == KeyAction::Hold) {
        switch (ev->key) {
        case Key::Back:
        case Key::Escape:
            // return to menu
            appManager::instance().close_current_and_open("app_launcher_menu");
            break;
        case Key::Enter:
        case Key::Select:
            run_embedded_sample();
            break;
        case Key::Up:
            scroll++;
            redraw_text();
            break;
        case Key::Down:
            if (scroll > 0) scroll--;
            redraw_text();
            break;
        default:
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Registry entry (same pattern as PongApp)
// ---------------------------------------------------------------------------
static AppRegistration reg_vulcan([]() {
    AppManifest m;
    m.name = "VulcanApp";
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<VulcanApp>(cfg);
    };
    return m;
}());
