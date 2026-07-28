#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "hardware/drivers/psram_std/psram_std.hpp"
#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/core/notification_sys/rs_notif_dispatcher.h"
#include "os_code/core/window_env/MWenv.hpp"
#include "esp_task_wdt.h"
#include "esp_log.h"
//lol
static const char* TAG = "AppFramework";

AppBase::AppBase(const ApplicationConfig& cfg)
    : cfg_(cfg),
      appTickRateHZ(cfg.tick_rate_hz),
      window_(nullptr),
      task_handle_(nullptr)
{
    ESP_LOGI(TAG, "App created: %s", cfg.name);
}

AppBase::~AppBase() {
    stop_task();  // Ensure cleanup
}

void AppBase::start_task() {
    if (task_handle_ != nullptr) {
        ESP_LOGW(TAG, "App %s already has a task", get_app_name());
        return;
    }
    ESP_LOGI(TAG, "attempting Creating task for app %s with stack %zu", cfg_.name, cfg_.stack_size_bytes);

    BaseType_t result = xTaskCreate(
        task_func,
        cfg_.name,
        cfg_.stack_size_bytes,
        this,
        cfg_.priority,
        &task_handle_
    );


    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task for app %s", get_app_name());
        task_handle_ = nullptr;
    } else {
        ESP_LOGI(TAG, "Task created for app %s", get_app_name());
        // Add task to watchdog (ignore error if already added)
        esp_task_wdt_add(task_handle_);
        on_start(); //invoke app local on start function calldown
        on_draw();
         
        WindowManager::getInstance().UpdateAll(true, true, false, false); //redraw all windows after starting the app, bit's added with a win
    }
}

void AppBase::stop_task() {
    should_stop_ = true;

    if (task_handle_ != nullptr) {
        esp_task_wdt_delete(task_handle_);

        // Wait for task to exit (max 1 second)
        TickType_t timeout = pdMS_TO_TICKS(1000);
        while (timeout > 0 && eTaskGetState(task_handle_) != eDeleted) {
            vTaskDelay(pdMS_TO_TICKS(10));
            timeout -= pdMS_TO_TICKS(10);
        }
        // Force delete if still alive
        if (eTaskGetState(task_handle_) != eDeleted) {
            ESP_LOGW(TAG, "Force deleting task for app %s", get_app_name());
            vTaskDelete(task_handle_);
        }
        task_handle_ = nullptr;
    }

    on_stop();
}

void AppBase::force_close() {
    should_stop_ = true;
    
    if (task_handle_ != nullptr) {
        esp_task_wdt_delete(task_handle_);
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
    
    on_stop();
}

// =======================================================
// Lifecycle Methods
// =======================================================

void AppBase::pause() {
    if (!paused_) {
        paused_ = true;
        on_pause();
    }
}

void AppBase::resume() {
    if (paused_) {
        paused_ = false;
        on_resume();
    }
}

//nothing is implemented here?




std::shared_ptr<DataPool> AppBase::establish_pool(size_t bytes, e_type_storage stype) {
    auto pool = DataPool::create_shared(bytes, stype, get_app_name());
    if (pool) {
        owned_pools.push_back(pool);
        on_outlet_established(pool.get());
    }
    return pool;
}

bool AppBase::on_outlet_established(DataPool* pool) {
    // Apps can override to react
    return true;
}

bool AppBase::on_inlet_established(DataPool* pool, Rshell_pipe_flowType flow) {
    return true;
}

void AppBase::request_check_pool(DataPool* pool) {
    // App-initiated poll of its pool
    if (pool) {
        PoolAccessToken token(pool, this, AccessMode::READ_ONLY);
        if (token.is_valid()) {
            // Example: process ring buffer
            if (pool->is_ring()) {
                // handle data...
            }
        }
    }
}

void AppBase::force_check_pool(DataPool* pool) {
    request_check_pool(pool);  // default behavior
}



void AppBase::bind_main_window(std::shared_ptr<Window> win) {
    window_ = win;
    if (win) {
        ESP_LOGD(TAG, "Window bound to app %s", get_app_name());
    }
}

bool AppBase::has_capability(AppCapability cap) const {
    return (static_cast<uint32_t>(cfg_.capabilities) & static_cast<uint32_t>(cap)) != 0;
}

// =======================================================
// Task Function
// =======================================================

void AppBase::task_func(void* arg) {
    AppBase* self = static_cast<AppBase*>(arg);
    if (!self) {
        ESP_LOGE(TAG, "Task started with null self pointer");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Task running for app %s", self->get_app_name());

    // Calculate tick period in ms
    uint32_t tick_period_ms = 1000 / self->appTickRateHZ;
    if (tick_period_ms == 0) tick_period_ms = 10;

    uint32_t last_tick = 0;

    while (!self->should_stop_) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t delta = now - last_tick;
        last_tick = now;

        if (!self->paused_) {
            self->tick_app(delta);
        }

        // Watchdog reset
        esp_task_wdt_reset();

        // Delay
        vTaskDelay(pdMS_TO_TICKS(tick_period_ms));
    }

    ESP_LOGI(TAG, "Task stopping for app %s", self->get_app_name());
    vTaskDelete(NULL);
}

// =======================================================
// Init (already exists)
// =======================================================

void AppBase::init() {
    // Registration is handled by appManager via manifest
    // Do NOT call appManager::instance().register_app() here
}