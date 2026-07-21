#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "os_code/core/notification_sys/rs_notif_dispatcher.h"
static const char* TAG = "AppManager";

#include "os_code/core/rShell/defaultAppList.hpp"

//linkages to system
appManager::appManager() : ref_wm(WindowManager::getInstance()) {
    ESP_LOGI(TAG, "AppManager initialized");
    notification_system_init();
}


//self management
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

//system cleanup of dead apps and pools; incomplete automatic memory management state
void appManager::cleanup_old_app(std::shared_ptr<AppBase> old_app) {
    if (!old_app) return;

    auto it = std::find(apps.begin(), apps.end(), old_app);
    if (it != apps.end()) apps.erase(it);

    auto name_it = std::find_if(running_apps.begin(), running_apps.end(),
        [&](const auto& p) { return p.second.lock() == old_app; });
    if (name_it != running_apps.end()) running_apps.erase(name_it);

    ESP_LOGI(TAG, "Cleaned up old app: %s", old_app->get_app_name());
}

void appManager::register_app(const std::shared_ptr<AppBase>& app) {
    apps.push_back(app);
}

void appManager::pause_app(const std::string& name) {
    if (auto app = get_app(name)) app->pause();
}

void appManager::resume_app(const std::string& name) {
    if (auto app = get_app(name)) app->resume();
}

void appManager::kill_app(const std::string& name) {
    if (auto app = get_app(name)) app->stop_task();
}

void appManager::force_kill_app(const std::string& name) {
    if (auto app = get_app(name)) app->force_close();
}








std::shared_ptr<AppBase> appManager::get_app(const std::string& name) {
    auto it = running_apps.find(name);
    if (it != running_apps.end()) {
        auto app = it->second.lock();
        if (app) return app;
        running_apps.erase(it);  // Clean up dead weak_ptr
    }
    return nullptr;
}

bool appManager::is_app_running(const std::string& name) {
    return get_app(name) != nullptr;
}



std::shared_ptr<AppBase> appManager::open_app(const std::string& name) {
    ESP_LOGI(TAG, "Opening app: %s", name.c_str());

    // Check if already running
    if (auto existing = get_app_by_name(name)) {
        set_focused_app(existing);
        return existing;
    }

    // Create and launch new
    auto app = create_app(name);
    if (app) {
        app->start_task();
        set_focused_app(app);
        running_apps[name] = app;
        ESP_LOGI(TAG, "Successfully launched: %s", name.c_str());
        return app;
    }

    ESP_LOGE(TAG, "Failed to open app: %s", name.c_str());
    return nullptr;
}

std::shared_ptr<AppBase> appManager::get_app_by_name(const std::string& name) {
    // First check running apps
    if (auto app = get_app(name)) {
        return app;
    }

    // Fall back to factory creation if registered
    auto it = app_factories.find(name);
    if (it != app_factories.end()) {
        return it->second();
    }

    return nullptr;
}





static void notification_task(void* pv) {
    ESP_LOGI("NOTIF_TASK", "Notification background task started");
    
    while (true) {
        notification_process();
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));  // Check every minute
    }
}

// Call this from main after boot
void start_notification_task() {
    xTaskCreate(notification_task, "notif_task", 4096, NULL, 2, NULL);
}







//=======================================================
//pool and pipe management================================
//=======================================================





//pool management
std::shared_ptr<DataPool> appManager::establish_pool(size_t bytes, e_type_storage stype) {
    return DataPool::create_shared(bytes, stype, "appmanager");
}

bool appManager::establish_outlet(std::shared_ptr<AppBase> app, size_t bytes, e_type_storage stype) {
    if (!app) return false;
    auto pool = app->establish_pool(bytes, stype);
    return pool != nullptr;
}

//note to self, need to check if multitarget works across afformentioned targets and those pipes
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

void appManager::force_check_pools() {
    for (auto& app : apps) {
        if (app) {
            // TODO: When owned_pools is accessible, loop here
            // for (auto& pool : app->owned_pools) {
            //     app->force_check_pool(pool.get());
            // }
        }
    }
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
    return create_pipe(source, targets, direct);
}














//=======================================================
//ELIF LOADING AND DYNAMIC REGISTRY

//=======================================================
void appManager::register_dynamic_app(const std::string& name, AppFactory factory) {
    app_factories[name] = factory;
    ESP_LOGI(TAG, "Dynamic app registered: %s", name.c_str());
}

std::shared_ptr<AppBase> appManager::create_dynamic_app(const std::string& name) {
    auto it = app_factories.find(name);
    if (it != app_factories.end()) {
        return it->second();
    }
    return nullptr;
}