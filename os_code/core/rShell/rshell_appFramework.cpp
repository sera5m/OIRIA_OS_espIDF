#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "os_code/core/window_env/MWenv.hpp"

static const char* TAG = "AppFramework";

AppBase::AppBase(const ApplicationConfig& cfg)
    : cfg_(cfg),
      name_storage_(cfg.name ? cfg.name : "app"),
      appTickRateHZ(cfg.tick_rate_hz)
{
    // Own the bytes. Callers often pass manifest.name.c_str().
    cfg_.name = name_storage_.c_str();
    ESP_LOGI(TAG, "App created: %s", cfg_.name);
}

AppBase::~AppBase() {
    stop_task();
}

void AppBase::start_task() {
    if (task_handle_ != nullptr) {
        ESP_LOGW(TAG, "App %s already has a task", get_app_name());
        return;
    }
    ESP_LOGI(TAG, "attempting Creating task for app %s with stack %zu", cfg_.name, cfg_.stack_size_bytes);

    self_stop_ = false;
    const BaseType_t result = xTaskCreate(
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
        esp_task_wdt_add(task_handle_);
        on_start();
        on_draw();
        WindowManager::getInstance().UpdateAll(true, true, false, false);
    }
}

void AppBase::exit_from_self() {
    on_stop();
    if (task_handle_ != nullptr) {
        esp_task_wdt_delete(task_handle_);
        task_handle_ = nullptr;
    }
    vTaskDelete(nullptr);
}

void AppBase::stop_task() {
    should_stop_ = true;

    if (task_handle_ != nullptr && task_handle_ == xTaskGetCurrentTaskHandle()) {
        // The task loop calls exit_from_self. Do not delete ourselves and continue.
        self_stop_ = true;
        return;
    }

    if (task_handle_ != nullptr) {
        for (int i = 0; i < 100 && task_handle_ != nullptr; ++i) {
            eTaskState st = eTaskGetState(task_handle_);
            if (st == eDeleted) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (task_handle_ != nullptr) {
            eTaskState st = eTaskGetState(task_handle_);
            if (st != eDeleted) {
                ESP_LOGW(TAG, "Force deleting task for app %s", get_app_name());
                esp_task_wdt_delete(task_handle_);
                vTaskDelete(task_handle_);
            }
            task_handle_ = nullptr;
        }
    }

    if (!self_stop_) on_stop();
}

void AppBase::force_close() {
    should_stop_ = true;
    if (task_handle_ != nullptr && task_handle_ == xTaskGetCurrentTaskHandle()) {
        self_stop_ = true;
        return;
    }
    if (task_handle_ != nullptr) {
        esp_task_wdt_delete(task_handle_);
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
    on_stop();
}

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

bool AppBase::on_outlet_established(DataPool* pool) {
    (void)pool;
    return true;
}

bool AppBase::on_inlet_established(DataPool* pool, Rshell_pipe_flowType flow) {
    (void)pool;
    (void)flow;
    return true;
}

void AppBase::request_check_pool(DataPool* pool) {
    if (!pool) return;
    PoolAccessToken token(pool, this, AccessMode::READ_ONLY);
    (void)token;
}

void AppBase::force_check_pool(DataPool* pool) {
    request_check_pool(pool);
}

void AppBase::bind_main_window(std::shared_ptr<Window> win) {
    window_ = std::move(win);
    if (!window_) return;
    ESP_LOGD(TAG, "Window bound to app %s", get_app_name());
    appManager::instance().on_main_window_bound(this);
}

bool AppBase::has_capability(AppCapability cap) const {
    return (static_cast<uint32_t>(cfg_.capabilities) & static_cast<uint32_t>(cap)) != 0;
}

bool AppBase::require_capability(AppCapability cap, const char* what) const {
    if (has_capability(cap)) return true;
    ESP_LOGW(TAG, "app '%s' missing capability for %s (declare it in the manifest)",
             get_app_name() ? get_app_name() : "?", what ? what : "operation");
    return false;
}

bool AppBase::request_gpio_access(const char* why) const {
    return require_capability(AppCapability::RAW_GPIO_ACCESS, why ? why : "gpio");
}

bool AppBase::request_wireless(const char* why) const {
    return require_capability(AppCapability::USES_WIRELESS, why ? why : "wireless");
}

bool AppBase::request_wake(const char* why) const {
    return require_capability(AppCapability::CAN_WAKE_DEVICE, why ? why : "wake");
}

std::shared_ptr<DataPool> AppBase::establish_pool(size_t bytes, e_type_storage stype) {
    if ((stype == STORAGE_MICROSD || stype == STORAGE_NVS || stype == STORAGE_EXT_NONVOL) &&
        !require_capability(AppCapability::USES_SD_CARD, "hard-storage pool")) {
        return nullptr;
    }
    auto pool = DataPool::create_shared(bytes, stype, get_app_name());
    if (pool) {
        pool->set_owner_identity(this, true);
        pool->attach();
        owned_pools.push_back(pool);
        on_outlet_established(pool.get());
    }
    return pool;
}

std::unique_ptr<PoolRing> AppBase::establish_ring(size_t bytes, e_type_storage stype) {
    if ((stype == STORAGE_MICROSD || stype == STORAGE_NVS || stype == STORAGE_EXT_NONVOL) &&
        !require_capability(AppCapability::USES_SD_CARD, "hard-storage ring")) {
        return nullptr;
    }
    if (!require_capability(AppCapability::ST_RING_CAPABLE, "establish_ring")) {
        return nullptr;
    }
    return PoolRing::create(bytes, stype, get_app_name(), this);
}

void AppBase::task_func(void* arg) {
    AppBase* self = static_cast<AppBase*>(arg);
    if (!self) {
        ESP_LOGE(TAG, "Task started with null self pointer");
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "Task running for app %s", self->get_app_name());

    uint32_t tick_period_ms = 1000 / (self->appTickRateHZ > 0 ? self->appTickRateHZ : 1);
    if (tick_period_ms == 0) tick_period_ms = 10;

    uint32_t last_tick = 0;

    while (!self->should_stop_) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t delta = now - last_tick;
        last_tick = now;

        if (!self->paused_) {
            self->tick_app(delta);
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(tick_period_ms));
    }

    ESP_LOGI(TAG, "Task stopping for app %s", self->get_app_name());
    if (self->self_stop_) {
        self->exit_from_self();
        return;
    }
    vTaskDelete(nullptr);
}

void AppBase::init() {
}

bool AppBase::save_state_to_rpool(const std::string& path) {
    if (!require_capability(AppCapability::USES_SD_CARD, "save_state_to_rpool")) return false;
    if (owned_pools.empty() || !owned_pools.front()) return false;
    return owned_pools.front()->save_to_rpool(path);
}

bool AppBase::load_state_from_rpool(const std::string& path) {
    if (!require_capability(AppCapability::USES_SD_CARD, "load_state_from_rpool")) return false;
    if (owned_pools.empty() || !owned_pools.front()) return false;
    return owned_pools.front()->load_from_rpool(path);
}
