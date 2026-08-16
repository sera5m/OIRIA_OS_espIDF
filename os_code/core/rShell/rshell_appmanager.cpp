#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "esp_log.h"
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
    if (focused_app) {
        std::string current_name = focused_app->get_app_name();
        kill_app(current_name);
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