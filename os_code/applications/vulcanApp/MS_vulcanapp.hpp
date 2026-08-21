#pragma once

// =============================================================================
// VulcanApp – minimal RS-VM runner for the watch (ESP32-S3 / AppBase)
// =============================================================================
// Shows VM print output on a scrollable text window. Runs an embedded sample
// (or SD script later). BACK exits; ENTER re-runs the sample.
// =============================================================================

#include <stdint.h>
#include <memory>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"

// RS-VM (watch tree path — adjust if your layout differs)
#include "os_code/core/rs_vm/vm/rs_vm.hpp"
#include "os_code/core/rs_vm/vm/rs_vm_parse.hpp"

class VulcanApp : public AppBase {
public:
    explicit VulcanApp(const ApplicationConfig& cfg);

    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void on_draw() override;

    void on_start() override;
    void on_stop() override;
    void on_pause() override;
    void on_resume() override;

    // Called from ESP host print hooks (user pointer → this)
    void append_log(const char* s, int len);
    void append_char(char c);

private:
    static constexpr int LOG_CAP = 2048;
    static constexpr int LINE_H  = 14;
    static constexpr int MAX_LINES_VISIBLE = 12;

    std::shared_ptr<Window> win;

    rsvm_t   vm{};
    bool     vm_ready = false;
    bool     ran_once = false;
    char     status_line[64] = {0};

    char     log_buf[LOG_CAP] = {0};
    int      log_len = 0;
    int      scroll  = 0;   // line offset from bottom

    void clear_log();
    void run_embedded_sample();
    void redraw_text();
};

// ESP host install (defined in rs_vm_host_esp.cpp)
extern "C" void rsvm_install_esp_host(rsvm_t* vm);
// Optional: install host with app-backed print sink
void rsvm_install_esp_host_ui(rsvm_t* vm, VulcanApp* app);
