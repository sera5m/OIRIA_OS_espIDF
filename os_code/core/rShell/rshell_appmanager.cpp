#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "esp_task_wdt.h"
#include "os_code/core/notification_sys/rs_notif_dispatcher.h"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/middle_layer/input/hid_t.h"
//#include "os_code/middle_layer/input/input_handler.hpp" 
#include "os_code/middle_layer/input/hid_t.h"






static const char* TAG = "AppManager";

// =======================================================

// Forward declarations
struct InputEvent;

// =======================================================
// Construction / Destruction
// =======================================================
appManager::appManager() : ref_wm(WindowManager::getInstance()) {
    ESP_LOGI(TAG, "AppManager initialized");
    notification_system_init();
}

appManager::~appManager() {
    DestroyAllApps();
}

appManager& appManager::instance() {
    static appManager inst;
    return inst;
}

void appManager::start_manager_task() {
    xTaskCreate([](void* p) {
        auto* self = static_cast<appManager*>(p);
        while (true) {
            self->update();
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }, "AppMgr", 4096, this, 5, &manager_task_);
}

void appManager::update() {
    for (auto it = running_apps.begin(); it != running_apps.end(); ) {
        if (it->second.expired()) {
            it = running_apps.erase(it);
        } else {
            ++it;
        }
    }
}

// =======================================================
// Registration
// =======================================================

void appManager::register_app(const AppManifest& manifest) {
    app_manifests[manifest.name] = manifest;
    ESP_LOGI(TAG, "Registered app: %s", manifest.name.c_str());
    
}

void appManager::register_dynamic_app(const AppManifest& manifest) {
    app_manifests[manifest.name] = manifest;
    ESP_LOGI(TAG, "Registered dynamic app: %s", manifest.name.c_str());
}

// =======================================================
// App Lookup
// =======================================================

std::shared_ptr<AppBase> appManager::get_app(const std::string& name) {
    auto it = running_apps.find(name);
    if (it != running_apps.end()) {
        auto app = it->second.lock();
        if (app) return app;
        running_apps.erase(it);  // clean expired
    }
    return nullptr;
}

bool appManager::is_app_running(const std::string& name) {
    return get_app(name) != nullptr;
}

std::shared_ptr<AppBase> appManager::get_app_by_name(const std::string& name) {
    if (auto app = get_app(name)) {
        return app;
    }
    return nullptr;
}
bool appManager::is_app_registered(const std::string& name) const {
    return app_manifests.find(name) != app_manifests.end();
}
// =======================================================
// Open / Launch
// =======================================================

std::shared_ptr<AppBase> appManager::open_app(const std::string& name) {
    ESP_LOGI(TAG, "Opening app: %s", name.c_str());

    // If already running, focus it
    if (auto existing = get_app(name)) {
        set_focused_app(existing);
        return existing;
    }

    // Find manifest
    auto it = app_manifests.find(name);
    if (it == app_manifests.end()) {
        ESP_LOGE(TAG, "App not registered: %s", name.c_str());
        return nullptr;
    }

    const AppManifest& manifest = it->second;

    // Build ApplicationConfig from manifest
    ApplicationConfig cfg;
    cfg.capabilities = manifest.capabilities;
    cfg.stack_size_bytes = manifest.stack_size_bytes;
    cfg.priority = manifest.priority;
    cfg.name = manifest.name.c_str();
    cfg.tick_rate_hz = manifest.tick_rate_hz;

    // Create the app using the manifest's factory
    auto app = manifest.create(cfg);
    if (!app) {
        ESP_LOGE(TAG, "Failed to create app: %s", name.c_str());
        return nullptr;
    }

    // Initialize and start
    app->init();
    app->start_task();

    // Store in running
    running_apps[name] = app;
    apps.push_back(app);
    set_focused_app(app);

    ESP_LOGI(TAG, "App launched: %s", name.c_str());
    return app;
}

// =======================================================
// Cleanup
// =======================================================

void appManager::cleanup_old_app(std::shared_ptr<AppBase> old_app) {
    if (!old_app) return;

    // Remove from apps vector
    auto it = std::find(apps.begin(), apps.end(), old_app);
    if (it != apps.end()) apps.erase(it);

    // Remove from running_apps
    for (auto it = running_apps.begin(); it != running_apps.end(); ) {
        if (it->second.lock() == old_app) {
            it = running_apps.erase(it);
        } else {
            ++it;
        }
    }

    // If this was the focused app, clear it
    if (focused_app == old_app) {
        focused_app.reset();
    }

    ESP_LOGI(TAG, "Cleaned up old app: %s", old_app->get_app_name());
}

void appManager::DestroyAllApps() {
    for (auto& app : apps) {
        if (app) app->force_close();
    }
    apps.clear();
    running_apps.clear();
    active_pipes.clear();
    ESP_LOGI(TAG, "All apps destroyed");
}

// =======================================================
// Control
// =======================================================

void appManager::pause_app(const std::string& name) {
    if (auto app = get_app(name)) app->pause();
}

void appManager::resume_app(const std::string& name) {
    if (auto app = get_app(name)) app->resume();
}

void appManager::kill_app(const std::string& name) {
    if (auto app = get_app(name)) {
        app->stop_task();
        cleanup_old_app(app);   // Remove from containers
    }
}

void appManager::force_kill_app(const std::string& name) {
    if (auto app = get_app(name)) {
        app->force_close();
        cleanup_old_app(app);
    }
}

void appManager::draw_all() {
    for (auto& app : apps) {
        if (app) app->on_draw();
    }
}





// =======================================================
// Focus Management
// =======================================================

void appManager::set_focused_app(std::shared_ptr<AppBase> app) {
    focused_app = app;
    if (app) {
        ESP_LOGD(TAG, "Focused app: %s", app->get_app_name());
    }
}

std::shared_ptr<AppBase> appManager::get_focused_app() const {
    return focused_app;
}

void appManager::route_input_to_focused(const InputEvent& ev) {
    if (focused_app) {
        focused_app->receive_event_input(&ev);
    }
}

void appManager::close_current_and_open(std::string name) {
    // `name` is by-value — safe after kill_app destroys the previous app / menu.
    if (focused_app) {
        const char* cn = focused_app->get_app_name();
        std::string current_name = cn ? cn : std::string{};
        if (!current_name.empty()) {
            kill_app(current_name);
        } else {
            auto doomed = focused_app;
            focused_app.reset();
            if (doomed) {
                doomed->request_stop();
                cleanup_old_app(doomed);
            }
        }
    }
    open_app(name);
}

void appManager::swap_to_app(std::shared_ptr<AppBase> new_app) {
    if (focused_app && focused_app != new_app) {
        focused_app->pause();
    }
    set_focused_app(new_app);
    if (new_app) new_app->resume();
}


std::vector<RegisteredAppInfo> appManager::list_registered_apps() const {
    std::vector<RegisteredAppInfo> out;
    out.reserve(app_manifests.size());
    for (const auto& kv : app_manifests) {
        RegisteredAppInfo info;
        info.name = kv.second.name.empty() ? kv.first : kv.second.name;
        info.display_name = kv.second.display_name.empty()
                                ? info.name
                                : kv.second.display_name;
        info.description = kv.second.description;
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(),
              [](const RegisteredAppInfo& a, const RegisteredAppInfo& b) {
                  return strcasecmp(a.display_name.c_str(), b.display_name.c_str()) < 0;
              });
    return out;
}
// =======================================================
// Pool Management
// =======================================================

std::shared_ptr<DataPool> appManager::establish_pool(size_t bytes, e_type_storage stype) {
    return DataPool::create_shared(bytes, stype, "appmanager");
}

bool appManager::establish_outlet(std::shared_ptr<AppBase> app, size_t bytes, e_type_storage stype) {
    if (!app) return false;
    auto pool = app->establish_pool(bytes, stype);
    return pool != nullptr;
}

bool appManager::establish_inlet(std::shared_ptr<AppBase> app, DataPool* pool, Rshell_pipe_flowType flow) {
    if (!app || !pool) return false;
    return app->on_inlet_established(pool, flow);
}



// =======================================================
// Pipe Management
// =======================================================

bool appManager::create_pipe(std::shared_ptr<AppBase> source,
    const std::vector<PipeTarget>& targets,
    Rshell_pipe_flowType flow) {
    if (!source || targets.empty()) return false;

    auto pipe = std::make_unique<RshellPipe>();
    pipe->source_app = source.get();
    pipe->mode = flow;
    pipe->id = "pipe_" + std::to_string(active_pipes.size());

    for (const auto& t : targets) {
        pipe->add_target(t);
    }

    active_pipes.push_back(std::move(pipe));
    ESP_LOGI(TAG, "Pipe created from %s to %zu targets", source->get_app_name(), targets.size());
    return true;
}

bool appManager::pipe_to_apps(std::shared_ptr<AppBase> from,
                              const std::vector<std::shared_ptr<AppBase>>& to_apps,
                              Rshell_pipe_flowType flow) {
    std::vector<PipeTarget> targets;
    for (const auto& app : to_apps) {
        if (app) targets.push_back(PipeTarget::from_app(app.get()));
    }
    return create_pipe(from, targets, flow);
}

bool appManager::pipe_to_pools(std::shared_ptr<AppBase> from,
                               const std::vector<DataPool*>& to_pools,
                               Rshell_pipe_flowType flow) {
    std::vector<PipeTarget> targets;
    for (auto pool : to_pools) {
        if (pool) targets.push_back(PipeTarget::from_pool(pool));
    }
    return create_pipe(from, targets, flow);
}

bool appManager::pipe_to_targets(std::shared_ptr<AppBase> from,
                                 const std::vector<PipeTarget>& targets,
                                 Rshell_pipe_flowType flow) {
    return create_pipe(from, targets, flow);
}

bool appManager::pipe_apps(std::shared_ptr<AppBase> from, std::shared_ptr<AppBase> to, Rshell_pipe_flowType flow) {
    std::vector<PipeTarget> targets;
    if (to) targets.push_back(PipeTarget::from_app(to.get()));
    return create_pipe(from, targets, flow);
}

bool appManager::connect_pipe(std::shared_ptr<AppBase> source,
                              std::shared_ptr<AppBase> target,
                              bool use_psram_ring) {
    std::vector<PipeTarget> targets;
    if (target) targets.push_back(PipeTarget::from_app(target.get()));
    // Use direct (1:1) or fan (1:N) - ring is a pool property, not pipe mode
    Rshell_pipe_flowType flow = use_psram_ring ? direct : direct;
    return create_pipe(source, targets, flow);
}

void appManager::force_check_pools() {
    for (auto& app : apps) {
        if (app) {
            for (auto& pool : app->get_owned_pools()) {
                app->force_check_pool(pool.get());
            }
        }
    }
}


// =============================================================================
// Vulcan secondary mode + serial teletype (fused from patches)
// Enable full RS-VM path by defining RSVM_IN_FIRMWARE in the component build.
// =============================================================================

#ifndef RSVM_IN_FIRMWARE
// Stubs so the tree links before rs_vm is wired into CMake.

int appManager::run_vulcan_script(const char* path) {
    if (!path || !path[0]) return -1;
    ESP_LOGW(TAG, "run_vulcan_script(%s): RS-VM not in build (define RSVM_IN_FIRMWARE)", path);
    return -1;
}

int appManager::send_vulcan_uart(uint8_t type, const uint8_t* payload, uint16_t len) {
    (void)type; (void)payload; (void)len;
    ESP_LOGW(TAG, "send_vulcan_uart: RS-VM/UART DOM not in build");
    return -1;
}

bool appManager::vulcan_mode_enabled() const {
    return false;
}

#else  // RSVM_IN_FIRMWARE

#include "os_code/core/rs_vm/vm/rs_vm.hpp"
#include "os_code/core/rs_vm/vm/rs_vm_parse.hpp"
#include "os_code/core/window_env/rs_dom_link.hpp"

extern "C" void rsvm_install_esp_host(rsvm_t* vm);

int appManager::run_vulcan_script(const char* path) {
    if (!path || !path[0]) return -1;
    rsvm_t vm;
    rsvm_init(&vm);
    rsvm_install_esp_host(&vm);
    rsvm_parse_err_t err{};
    rsvm_status_t st = rsvm_eval_file(&vm, path, &err);
    if (st != RSVM_OK) {
        ESP_LOGE(TAG, "vulcan %s: %s L%d", path, err.message, err.line);
        return (int)st;
    }
    return 0;
}

int appManager::send_vulcan_uart(uint8_t type, const uint8_t* payload, uint16_t len) {
    uint8_t pkt[1024];
    static uint16_t seq;
    size_t n = rsdom_pack(pkt, sizeof pkt, type, RSDOM_FLAG_LOGIC, seq++, payload, len);
    if (!n) return -1;
    // Wire to the collective UART when pins are configured:
    // uart_write_bytes(UART_NUM_x, pkt, n);
    ESP_LOGI(TAG, "send_vulcan_uart type=0x%02X len=%u pkt=%u", type, (unsigned)len, (unsigned)n);
    return (int)n;
}

bool appManager::vulcan_mode_enabled() const {
    return true;
}

#endif  // RSVM_IN_FIRMWARE

// ----- Serial terminal (always present; uses run_vulcan_script / feed) -----

void appManager::start_serial_terminal() {
    if (terminal_task_) return;
    isConnectedToSerialMonitor = true;
    BaseType_t ok = xTaskCreate(terminal_task_fn, "terminal", 8192, this, 5, &terminal_task_);
    if (ok != pdPASS) {
        terminal_task_ = nullptr;
        isConnectedToSerialMonitor = false;
        ESP_LOGE(TAG, "terminal task create failed");
        return;
    }
    ESP_LOGI(TAG, "serial terminal started (isConnectedToSerialMonitor=1)");
}

void appManager::stop_serial_terminal() {
    isConnectedToSerialMonitor = false;
    if (terminal_task_) {
        TaskHandle_t t = terminal_task_;
        terminal_task_ = nullptr;
        vTaskDelete(t);
    }
}

int appManager::load_serial_script(const char* path) {
    if (!path || serial_script_busy_) return -1;
    FILE* f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "load_serial_script: cannot open %s", path);
        return -1;
    }
    size_t n = fread(serial_script_buf_, 1, kSerialScriptMax - 1, f);
    fclose(f);
    serial_script_buf_[n] = 0;
    serial_script_len_ = n;
    return feed_serial_source(serial_script_buf_, n);
}

int appManager::feed_serial_source(const char* src, size_t len) {
    if (!src || serial_script_busy_) return -1;
    serial_script_busy_ = true;
    int rc = -1;
#if defined(RSVM_IN_FIRMWARE)
    rsvm_t vm;
    rsvm_init(&vm);
    rsvm_install_esp_host(&vm);
    rsvm_parse_err_t err{};
    char* copy = (char*)malloc(len + 1);
    if (!copy) {
        serial_script_busy_ = false;
        return -1;
    }
    memcpy(copy, src, len);
    copy[len] = 0;
    rsvm_status_t st = rsvm_eval(&vm, copy, &err);
    free(copy);
    if (st != RSVM_OK) {
        ESP_LOGE(TAG, "serial vulcan err L%d: %s", err.line, err.message);
        rc = (int)st;
    } else {
        rc = 0;
    }
#else
    (void)len;
    ESP_LOGW(TAG, "feed_serial_source: RS-VM not in build, %u bytes ignored", (unsigned)len);
    rc = -1;
#endif
    serial_script_busy_ = false;
    return rc;
}

void appManager::terminal_task_fn(void* arg) {
    auto* self = static_cast<appManager*>(arg);
    char line[256];
    size_t li = 0;
    char accum[kSerialScriptMax];
    size_t ai = 0;

    while (self && self->isConnectedToSerialMonitor) {
#if defined(RSVM_IN_FIRMWARE) && defined(CONFIG_ESP_CONSOLE_UART_NUM)
        // Prefer real UART when available
        uint8_t c = 0;
        int n = uart_read_bytes((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, &c, 1, pdMS_TO_TICKS(50));
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
#else
        // No UART driver in this TU — idle until stopped (host can still feed_serial_source)
        vTaskDelay(pdMS_TO_TICKS(200));
        continue;
#endif
        if (c == '\r') continue;
        if (c != '\n') {
            if (li + 1 < sizeof line) line[li++] = (char)c;
            continue;
        }
        line[li] = 0;
        li = 0;
        if (line[0] == 0) continue;

        if (strncmp(line, "run ", 4) == 0) {
            self->load_serial_script(line + 4);
            ai = 0;
            continue;
        }
        if (strcmp(line, ".") == 0) {
            accum[ai] = 0;
            self->feed_serial_source(accum, ai);
            ai = 0;
            continue;
        }
        if (strcmp(line, "clear") == 0) {
            ai = 0;
            continue;
        }

        size_t L = strlen(line);
        if (ai + L + 2 < sizeof accum) {
            memcpy(accum + ai, line, L);
            ai += L;
            accum[ai++] = '\n';
            accum[ai] = 0;
        }
        if (L > 0 && line[L - 1] == ';') {
            self->feed_serial_source(line, L);
            ai = 0;
        }
    }
    vTaskDelete(nullptr);
}
