#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"  // for appManager access
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "os_code/core/rShell/defaultAppList.hpp"
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

void AppBase::init() {
    appManager::instance().register_app(shared_from_this());
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