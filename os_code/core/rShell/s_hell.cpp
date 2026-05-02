// app_framework.cpp


     
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/middle_layer/input/input_devs_agg.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"


#include "os_code/core/rShell/s_hell.hpp"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "AppFramework";

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
    
    while (true) {
        tick_app(interval_ms);
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
}

// -------------------------------------------------------------------
// MyWatchApp example



///================================segment class appmanager-=======================
appManager::appManager()
    : ref_wm(WindowManager::getInstance()) //now with perminant binding for the silly
{
    ESP_LOGW(TAG, "started application manager task");
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

void appManager::set_focused_app(std::shared_ptr<AppBase> app) {

    auto old = focused_app;   // SAVE OLD FIRST
    
    focused_app = app;

    ESP_LOGI(TAG, "Focus set to app: %s",
             app ? app->get_app_name() : "none");

    // disable highlight on old app
    if (old) {
        if (auto win = old->get_window()) {
            win->window_highlighted = false;
        }
    }

    // enable highlight on new app
    if (focused_app) {
        if (auto win = focused_app->get_window()) {
            win->window_highlighted = true;
        }
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