#pragma once

#include <cstdint>
#include <memory>
#include <atomic>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
  // Make sure this is before AppBase in all files
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/core/rShell/streams/rshell_pool.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/core/rShell/streams/rshell_pipe.hpp"
//lol
//#include "os_code/core/rShell/rshell_appmanager.hpp"//warning:circular dependency

#include "hardware/drivers/psram_std/psram_std.hpp"
#include "os_code/middle_layer/input/hid_t.h"
#include "os_code/core/notification_sys/rs_notif_dispatcher.h"


// Forward declarations
class Window;
class DataPool;
class appManager;
struct DataItem;

using AppCapabilities = uint32_t;


// AppCapability enum (moved from old s_hell.hpp)
enum class AppCapability : uint32_t {
    NONE                = 0,
    MINIMIZABLE         = 1 << 0,
    FULLSCREEN          = 1 << 1,
    CONVERTIBLE_TO_TRAY = 1 << 2,
    SLEEPABLE           = 1 << 3,
    CAN_WAKE_DEVICE     = 1 << 4,
    USES_WIRELESS       = 1 << 5,
    USES_SD_CARD        = 1 << 6,
    RAW_GPIO_ACCESS     = 1 << 7,
    NEEDS_WINDOW        = 1 << 8,
    NEEDS_MULTI_WINDOW  = 1 << 9,
    SINGLETHREADED      = 1 << 10,
    STREAM_IN_CAPABLE   = 1 << 11,
    STREAM_OUT_CAPABLE  = 1 << 12,
    ST_RING_CAPABLE     = 1 << 13,
    ST_PF_CAPABLE       = 1 << 14,
    ST_PREF_RT_IPC      = 1 << 15
};




inline AppCapability operator|(AppCapability a, AppCapability b) {
    return static_cast<AppCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline AppCapability operator&(AppCapability a, AppCapability b) {
    return static_cast<AppCapability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}











struct ApplicationConfig {
    AppCapabilities capabilities;
    size_t stack_size_bytes = 4096;
    UBaseType_t priority = 5;
    const char* name = "appname";
    int tick_rate_hz = 10;
};

using AppCapabilities = uint32_t;

struct AppManifest {
    std::string name;
    std::string display_name;
    std::string description;
    AppCapabilities capabilities;
    size_t stack_size_bytes = 4096;
    int priority = 5;           // Use int instead of UBaseType_t to avoid FreeRTOS include
    int tick_rate_hz = 10;
    bool is_system_app = true;

    using EntryPoint = std::function<std::shared_ptr<AppBase>(const ApplicationConfig&)>;
    EntryPoint create;
};



using AppFactory = std::function<std::shared_ptr<AppBase>(const ApplicationConfig&)>;  // now takes config


class AppBase : public std::enable_shared_from_this<AppBase> {
public:
    explicit AppBase(const ApplicationConfig& cfg);
    virtual ~AppBase();






    void init();          // do NOT call register_app here
    void start_task();
    void stop_task();
    void force_close();

    virtual void pause();
    virtual void resume();

    virtual void on_start() {}
    virtual void on_stop() {}
    virtual void on_pause() {}
    virtual void on_resume() {}
    virtual void on_draw() {}


 // Syscalls
    virtual void on_syscall(uint32_t syscall_id, void* data) {}
    virtual bool send_data_upstream(const void* data, size_t size) { return false; }

    virtual void tick_app(uint32_t delta_ms) = 0;
    virtual void receive_event_input(const void* event) = 0;
    virtual void on_stream_data(const DataItem* item) {}

    void bind_main_window(std::shared_ptr<Window> win);
    std::shared_ptr<Window> get_main_window() const { return window_; }

    bool has_capability(AppCapability cap) const;
    const char* get_app_name() const { return cfg_.name; }

    std::shared_ptr<DataPool> establish_pool(size_t bytes, e_type_storage stype);
    bool save_state_to_rpool(const std::string& path);
    bool load_state_from_rpool(const std::string& path);

    virtual bool on_outlet_established(DataPool* pool);
    virtual bool on_inlet_established(DataPool* pool, Rshell_pipe_flowType flow);
    virtual void request_check_pool(DataPool* pool);
    virtual void force_check_pool(DataPool* pool);

    const std::vector<std::shared_ptr<DataPool>>& get_owned_pools() const {
        return owned_pools;
    }

protected:
    std::atomic<bool> should_stop_{false};
    std::atomic<bool> paused_{false};

    ApplicationConfig cfg_;
    std::shared_ptr<Window> window_;
    TaskHandle_t task_handle_ = nullptr;
    int appTickRateHZ;

    std::vector<std::shared_ptr<DataPool>> owned_pools;

private:
    static void task_func(void* arg);
};