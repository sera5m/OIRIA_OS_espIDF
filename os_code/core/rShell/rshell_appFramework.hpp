#pragma once

#include <cstdint>
#include <memory>
#include <atomic>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/core/rShell/streams/rshell_pool.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/core/rShell/streams/rshell_pipe.hpp"
#include "os_code/core/rShell/defaultAppList.hpp"
//#include "os_code/core/rShell/rshell_appmanager.hpp"//warning:circular dependency

// Forward declarations
class Window;
class appManager; //not included but forwarded

// Application configuration


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


using AppCapabilities = uint32_t;

inline AppCapability operator|(AppCapability a, AppCapability b) {
    return static_cast<AppCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline AppCapability operator&(AppCapability a, AppCapability b) {
    return static_cast<AppCapability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}


struct ApplicationConfig {
    AppCapabilities capabilities;
    size_t           stack_size_bytes = 4096;
    UBaseType_t      priority        = 5;
    const char*      name            = "appname";
    int              tick_rate_hz    = 10;
};





// Base App class - lives in rshell_appFramework
class AppBase : public std::enable_shared_from_this<AppBase> {
public:
    explicit AppBase(const ApplicationConfig& cfg);
    virtual ~AppBase();


    // Dynamic registration support==EXPEREMENTAL,PENDING=====
    static bool dynamic_register_app(const std::string& name, AppFactory factory);
    static std::shared_ptr<AppBase> create_dynamic_app(const std::string& name);
    // Dynamic registration
    void register_dynamic_app(const std::string& name, AppFactory factory);
    std::shared_ptr<AppBase> create_dynamic_app(const std::string& name);




    //lifecycle management
    void init();
    void start_task();
    void stop_task();           // graceful (SIGTERM-like)
    void force_close();         // immediate (SIGKILL-like)

    virtual void pause();
    virtual void resume();
    //end lifecycle management



    // Callbacks for apps to override: run on exec on lifecycle
    virtual void on_start() {}
    virtual void on_stop() {}
    virtual void on_pause() {}
    virtual void on_resume() {}
    virtual void on_before_close();
    //end lifecycle callbacks

    //update methods
    virtual void tick_app(uint32_t delta_ms) = 0;
    //update hid
    virtual void receive_event_input(const void* event) = 0;
    //update visuals and windows
    virtual void on_draw() {}
    
    void bind_main_window(std::shared_ptr<Window> win);
    std::shared_ptr<Window> get_main_window() const { return window_; }



    // Capabilities & state
    bool has_capability(AppCapability cap) const;
    const char* get_app_name() const { return cfg_.name; }
    bool is_paused() const { return paused_; }


    // Streaming
    virtual void on_stream_data(const DataItem* item) {}
    void publish(DataItem* item);


    // Pool & Stream integration
    std::shared_ptr<DataPool> establish_pool(size_t bytes, e_type_storage stype);
    bool on_outlet_established(DataPool* pool);
    bool on_inlet_established(DataPool* pool, Rshell_pipe_flowType flow);

    void request_check_pool(DataPool* pool);   // app-initiated check
    void force_check_pool(DataPool* pool);     // manager-forced

    
    


protected:
    void run();  // main loop - pure logic

    std::atomic<bool> should_stop_{false};
    std::atomic<bool> paused_{false};

    ApplicationConfig cfg_;
    std::shared_ptr<Window> window_;
    TaskHandle_t task_handle_ = nullptr;
    int appTickRateHZ;

    //msc items owned by the app
std::vector<std::shared_ptr<DataPool>> owned_pools;

private:
    static void task_func(void* arg);
};
