// app_framework.cpp


     



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
AppBase::AppBase(const ApplicationConfig& cfg) : cfg_(cfg) {

    // Create a window (replace with actual window creation)
    // window_ = std::make_shared<Window>(...);
    ESP_LOGI(TAG, "AppBase created with capabilities 0x%08lx", cfg_.capabilities);
}

AppBase::~AppBase() {
    stop_task();
}

void AppBase::init() {
    appManager::instance().register_app(shared_from_this());
}

void AppBase::start_task() {
    if (task_handle_ != nullptr) {
        ESP_LOGW(TAG, "Task already running");
        return;
    }
    BaseType_t res = xTaskCreate(
        task_func,
        cfg_.name,
        cfg_.stack_size_bytes,
        this,               // pass 'this' as argument
        cfg_.priority,
        &task_handle_
    );
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task for app");
        task_handle_ = nullptr;
    } else {
        ESP_LOGI(TAG, "App task started");
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
    // Notify that the app is starting
    on_start();

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period_ms = 50;   // 20 Hz tick – adjust as needed

    while (true) {
        // Call the derived tick_app with the actual elapsed time
        tick_app(period_ms);

        // Delay for the remainder of the period
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(period_ms));
    }

    on_stop(); // never reached in this simple loop
}

// -------------------------------------------------------------------
// MyWatchApp example



///================================segment class appmanager-=======================
appManager::appManager() {
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