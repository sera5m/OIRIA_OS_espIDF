// app_framework.cpp


     
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/middle_layer/input/input_devs_agg.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"
#include <functional>
#include "esp_task_wdt.h" 
#include "os_code/core/rShell/s_hell.hpp"
#include "esp_log.h"
#include <cstring>
#include "os_code/core/notification_sys/rs_notif_dispatcher.h"

static const char* TAG = "AppFramework";



//this function so we can see the fuck going on here
void print_stack_usage(const char* task_name) {
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    UBaseType_t high_water = uxTaskGetStackHighWaterMark(task);
    ESP_LOGI("STACK", "%s: High water mark = %u words (%u bytes free)", 
             task_name, high_water, high_water * 4);
}





// -------------------------------------------------------------------
// Dummy implementations of the API functions (replace with your actual system calls)
void request_hid_target_focus_to(AppBase* self, bool allow_for_others) {
    // Notify the window manager / input system
    ESP_LOGI(TAG, "Request HID focus for app %p, allow_others=%d", self, allow_for_others);
}

void request_stack_size_change(size_t new_bytes) {
    ESP_LOGI(TAG, "Request stack size change to %zu bytes (not implemented)", new_bytes);
    // In a real system you would need to recreate the task or use a custom allocator
}

void request_priority(int new_priority) {
    if (xTaskGetCurrentTaskHandle() != nullptr) {
        vTaskPrioritySet(nullptr, new_priority);
        ESP_LOGI(TAG, "Changed current task priority to %d", new_priority);
    } else {
        ESP_LOGW(TAG, "Cannot change priority – not in a FreeRTOS task context");
    }
}

// -------------------------------------------------------------------
// AppBase implementation
AppBase::AppBase(const ApplicationConfig& cfg) 
    : appTickRateHZ(cfg.tick_rate_hz),  // ← USE cfg value
      cfg_(cfg) 
{
    ESP_LOGI(TAG, "app created with tick rate %d Hz", appTickRateHZ);
}

AppBase::~AppBase() {
    stop_task();
}

void AppBase::init() {
    appManager::instance().register_app(shared_from_this());
    int appTickRateHZ=100;
}

void AppBase::start_task() {
    if (task_handle_ != nullptr) {
        ESP_LOGW(TAG, "Task already running");
        return;
    }

    BaseType_t res;

    if (has_capability(AppCapability::PINNED_TO_CORE)) {
        res = xTaskCreatePinnedToCore(
            task_func,
            cfg_.name,
            cfg_.stack_size_bytes,
            this,
            cfg_.priority,
            &task_handle_,
            1
        );
    } else {
        res = xTaskCreate(
            task_func,
            cfg_.name,
            cfg_.stack_size_bytes,
            this,
            cfg_.priority,
            &task_handle_
        );
    }

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task for app");
        task_handle_ = nullptr;
    } else {
        ESP_LOGI(TAG, "Application task started");
    }
}

void AppBase::stop_task() {
    if (task_handle_ != nullptr) {
        on_before_close();           // ← safe cleanup
        vTaskDelay(pdMS_TO_TICKS(30)); // give drawing time to stop
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
        ESP_LOGI(TAG, "App task stopped");
    }
}




void AppBase::task_func(void* arg) {
    AppBase* self = static_cast<AppBase*>(arg);
    self->run();
    // If run() returns, delete the task
    vTaskDelete(nullptr);
}

void AppBase::run() {
    on_start();
    
    const uint32_t interval_ms = 1000 / appTickRateHZ;
    ESP_LOGI(TAG, "run() starting with interval %lu ms", interval_ms);
    
    while (!should_stop_) {
        tick_app(interval_ms);
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
    on_stop();
    vTaskDelete(nullptr);
}

// -------------------------------------------------------------------
// MyWatchApp example



///================================segment class appmanager-=======================
appManager::appManager()
    : ref_wm(WindowManager::getInstance()) //now with perminant binding for the silly
{
    ESP_LOGW(TAG, "started application manager task");
    notification_system_init();
}

// destructor
appManager::~appManager() {
    ESP_LOGW(TAG, "stopping application manager task");
    DestroyAllApps();
}

appManager& appManager::instance() {
    static appManager instance; // created once, thread-safe because that's what the page i had said
    return instance;
}

void appManager::register_app(const std::shared_ptr<AppBase>& app) {
    apps.push_back(app);
}



void appManager::draw_all() {
    for (auto& app : apps) {
        if (app) {
            app->on_draw();
        }
    }
}



void appManager::DestroyAllApps() {
    for (const auto& app : apps) {
        if (app) {
            app->force_close();
        }
    }

    apps.clear();
}
void AppBase::bind_main_window(std::shared_ptr<Window> win) {
    if (window_) {
        ESP_LOGW(TAG, "App %s already has a window bound", get_app_name());
    }
    window_ = std::move(win);
    ESP_LOGI(TAG, "App %s bound window %s", get_app_name(), 
             window_ ? window_->Currentcfg.name : "nullptr");
}

void appManager::set_focused_app(std::shared_ptr<AppBase> app) {
    auto old = focused_app;
    focused_app = app;

    ESP_LOGI(TAG, "<<<<<<<<<<<<<<<<<<<Focus changed → %s", app ? app->get_app_name() : "none");

    // Old app loses focus
    if (old && old != app) {
        if (auto window_ = old->get_main_window()) {
            window_->window_highlighted = false;
            window_->dirty = true;           // important
        }
        old->on_focus_lost();
    }

    // New app gains focus
    if (focused_app) {
        if (auto window_ = focused_app->get_main_window()) {
            window_->window_highlighted = true;
            window_->dirty = true;
            // Optional: bring to front
            // ref_wm.BringToFront(win);  // if you have this method
        } else {
            ESP_LOGW(TAG, "Focused app %s has no window bound!", focused_app->get_app_name());
        }
        focused_app->on_focus_gained();
    }
}

std::shared_ptr<AppBase> appManager::get_focused_app() const {
    return focused_app;
}

void appManager::route_input_to_focused(const InputEvent& ev) {
    if (focused_app) {
        focused_app->receive_event_input(&ev);
    } else {
        ESP_LOGW(TAG, "No focused app to receive input");
    }
}

// In s_hell.cpp
void appManager::register_app_type(const std::string& name, AppFactory factory) {
    app_factories[name] = factory;
}

std::shared_ptr<AppBase> appManager::create_app(const std::string& name) {
    auto it = app_factories.find(name);
    if (it != app_factories.end()) {
        auto app = it->second();
        if (app) {
            app->init();
            ESP_LOGI(TAG, "Created app: %s", name.c_str());
        }
        return app;
    }
    ESP_LOGE(TAG, "Unknown app type: %s", name.c_str());
    return nullptr;
}


std::shared_ptr<AppBase> appManager::launch_app(const std::string& name) {
    // Check if already running
    if (auto existing = get_app(name)) {
        ESP_LOGI(TAG, "App %s already running, focusing it", name.c_str());
        set_focused_app(existing);
        return existing;
    }
    
    auto app = create_app(name);
    if (app) {
        app->start_task();
        set_focused_app(app);
        running_apps[name] = app;  // Store weak_ptr
        ESP_LOGI(TAG, "Launched app: %s", name.c_str());
    }
    return app;
}

void appManager::swap_to_app(std::shared_ptr<AppBase> new_app) {
    if (focused_app) {
        focused_app->stop_task();   // stop the current one
    }
    new_app->start_task();
    set_focused_app(new_app);
}

void appManager::close_current_and_open(const std::string& name) {
    ESP_LOGI(TAG, "Closing current and opening: %s", name.c_str());
    
    auto old_app = focused_app;
    
    if (old_app) {
        old_app->on_before_close();
        vTaskDelay(pdMS_TO_TICKS(50));
        old_app->stop_task();
    }
    
    auto new_app = launch_app(name);
    
    if (new_app && old_app && old_app != new_app) {
        auto it = std::find(apps.begin(), apps.end(), old_app);
        if (it != apps.end()) {
            apps.erase(it);
        }
    }
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