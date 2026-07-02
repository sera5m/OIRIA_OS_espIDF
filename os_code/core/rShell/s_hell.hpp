// app_framework.h
#pragma once

#include <cstdint>
#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/middle_layer/input/input_devs_agg.hpp"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/core/window_env/MWenv.hpp" //for added linkage to window manager singleton
#include <vector>
#include <memory>

#include <functional>
#include <unordered_map>
#include <string>

//shell addons
#include "os_code/core/notification_sys/rs_notif_dispatcher.h"


//assistance functrion========================
void print_stack_usage(const char* task_name);


//=============================================

namespace psram {
    struct EventRingBuffer;  // defined in .cpp
}




// Registration macro - must be used at file scope (outside any function)
#define REGISTER_APP(CLASS, NAME, CONFIG_BUILDER) \
    static bool _registered_##CLASS = []() { \
        appManager::instance().register_app_type(NAME, []() { \
            return std::make_shared<CLASS>(CONFIG_BUILDER()); \
        }); \
        return true; \
    }()





// -------------------------------------------------------------------
// Application configuration bitmask (order fixed as requested)
enum class AppCapability : uint32_t {
    NONE                = 0,
    MINIMIZABLE         = 1 << 0,   // can be minimized
    FULLSCREEN          = 1 << 1,   // supports full screen mode
    CONVERTIBLE_TO_TRAY = 1 << 2,   // can become a tray icon

    SLEEPABLE           = 1 << 3,   // can be put to sleep
    CAN_WAKE_DEVICE     = 1 << 4,   // can wake the device from sleep

    USES_WIRELESS       = 1 << 5,   // requires Wi‑Fi / BT
    USES_SD_CARD        = 1 << 6,   // accesses SD card
    RAW_GPIO_ACCESS     = 1 << 7,   // touches GPIO directly

    NEEDS_WINDOW        = 1 << 8,   // requires at least one window
    NEEDS_MULTI_WINDOW  = 1 << 9,    // needs more than one window

    SINGLETHREADED      =   1<<10,  //PIN TO CORE (will need to be specified which one) 

    STREAM_IN_CAPABLE   = 1<<11, //can take input data directly in
    STREAM_OUT_CAPABLE = 1<<12, //it's an output for streams
    ST_RING_CAPABLE = 1<<13, //CAN access fifo arbitrary data ring buffers
    ST_PF_CAPABLE= 1<<14, //pumped style flow with notifies for lower laten. 
    ST_PREF_RT_IPC= 1<<15 //prefers real time interproscess messaging instead of idle ring grabs

   
};
inline AppCapability operator|(AppCapability a, AppCapability b)
{
    return static_cast<AppCapability>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline AppCapability operator&(AppCapability a, AppCapability b)
{
    return static_cast<AppCapability>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
//implementation in proscess
// Bitmask type
using AppCapabilities = uint32_t;



struct ApplicationConfig {
    AppCapabilities capabilities;
    size_t           stack_size_bytes = 4096;
    UBaseType_t      priority        = 5;
    const char*      name            = "appname";
    int              tick_rate_hz    = 10;  
};

// Forward declarations
class Window;        // your existing window class
class AppBase;       // declared below
class appManager; //foward dec so apps can automatically report to appmanager

// -------------------------------------------------------------------
// API functions that apps can invoke
void request_hid_target_focus_to(AppBase* self, bool allow_for_others);
void request_stack_size_change(size_t new_bytes);
void request_priority(int new_priority);

// -------------------------------------------------------------------
// Base class for all applications which will never work anyway
class AppBase : public std::enable_shared_from_this<AppBase> {
public:


void request_stop() { should_stop_ = true; }
virtual void on_before_close() {
    if (window_) {
        window_->HaltDrawing();
    }
}


void bind_main_window(std::shared_ptr<Window> win);
std::shared_ptr<Window> get_main_window() const { return window_; }  // renamed for clarity

const char* get_app_name() const { return cfg_.name; } //getter because we love input handling so much!!!
    explicit AppBase(const ApplicationConfig& cfg);
    virtual ~AppBase();
    void init();
    // Lifecycle methods (override in derived classes)
    virtual void tick_app(uint32_t delta_ms) = 0;          // called periodically
    virtual void receive_event_input(const void* event) = 0; // input event
    virtual void force_tick(){} //
    virtual void suspend() = 0;                            // suspend operation
    virtual void force_close() = 0;                        // immediate termination

    // Optional Linux‑like methods (default = do nothing)
    virtual void on_start() {}
    virtual void on_stop()  {}
    virtual void on_pause() {}
    virtual void on_resume(){}
    virtual void on_draw(){}



    //methods for focusing and input condition swap
    virtual void on_focus_gained() {}
    virtual void on_focus_lost() {}
    // Streaming integration
    virtual void on_stream_data(const DataItem* item) {}   // default no-op
    virtual void publish(DataItem* item);                  // helper
    
    // Pipe / sharing capabilities (already in your enum - good)
    // Window access
    virtual void on_pipe_establish();
    virtual void on_pipe_destroyed();
    
    // Configuration queries
    AppCapabilities get_capabilities() const { return cfg_.capabilities; }
    bool has_capability(AppCapability cap) const {
        return (static_cast<uint32_t>(cfg_.capabilities) & static_cast<uint32_t>(cap)) != 0;
    }

    // Task control
    void start_task();
    void stop_task();
    int appTickRateHZ; //tick rate of the app in hz

protected:
std::atomic<bool> should_stop_{false};
    ApplicationConfig cfg_;
    std::shared_ptr<Window> window_;   // smart pointer to the app's main window
    TaskHandle_t task_handle_ = nullptr;

    // FreeRTOS task function (static wrapper)
    static void task_func(void* arg);
    void run();  // internal loop that calls tick_app periodically
};

// --------------------------------appmanager class-----------------------------------
//note to self since this is the first time i'm using polymorphic storage for derived classes
//std::vector<std::shared_ptr<AppBase>> apps; //stores app base, but base classes 
/*
apps.push_back(std::make_shared<WatchApp>()); //then make a shared app and push back a shared app into it
*/ //remember,watchapp IS technically appbase, but with some other shit
//A derived class object can be accessed through a base class pointer, 
//and virtual functions ensure the correct derived implementation is called.



// /-------------------------------------------------------------------
// appManager class
class appManager {
public:
    using AppFactory = std::function<std::shared_ptr<AppBase>()>;
    
    static appManager& instance();
    
    void register_app(const std::shared_ptr<AppBase>& app);
    void draw_all();
    void DestroyAllApps();
    
    void set_focused_app(std::shared_ptr<AppBase> app);
    std::shared_ptr<AppBase> get_focused_app() const;
    void route_input_to_focused(const InputEvent& ev);
    
    // App factory methods
    void register_app_type(const std::string& name, AppFactory factory);
    std::shared_ptr<AppBase> create_app(const std::string& name);
    std::shared_ptr<AppBase> launch_app(const std::string& name);
    void swap_to_app(std::shared_ptr<AppBase> new_app);
    void close_current_and_open(const std::string& name);
    
    // Legacy swap methods
    //void swap_task(std::shared_ptr<AppBase> close, std::shared_ptr<AppBase> open);
    void close_this_and_open_menu(std::shared_ptr<AppBase> self);
    std::shared_ptr<AppBase> get_app(const std::string& name);
    bool is_app_running(const std::string& name);


    // Pipe management
bool pipe_apps(std::shared_ptr<AppBase> from, std::shared_ptr<AppBase> to, Rshell_pipe_flowType flow);
bool pipe_to_streamcore(std::shared_ptr<AppBase> from);  // convenience
    

    //functions for establishing outlets and inlet
    bool can_establish_outlet(std::shared_ptr<AppBase> app, bool isRing);
     //is this stream data or is it in grab chunks in a linked psram segment as a cache ring be it in ram or cache
    bool can_establish_inlet(std::shared_ptr<AppBase> app,bool isRing);

    //establish the connection
    bool establish_outlet(std::shared_ptr<AppBase> app, bool isRing);
    bool establish_inlet(std::shared_ptr<AppBase> app,bool isRing);

    //std::unique_ptr<std::byte[]> establish_pool(std::size_t bytes); //return owning smart pointer
    using PoolPtr = std::unique_ptr<std::byte[]>;

    PoolPtr establish_pool(std::size_t bytes, e_type_storage stype);

//how the fuck do i make pipes have a target that branches? with a... list of targets? but that would need to be mutable
bool pipe_apps(std::shared_ptr<AppBase> input, std::shared_ptr<AppBase> output);
bool pipe_apps(std::shared_ptr<AppBase> output/*i need to have the ability to have multiple targets!*/, Rshell_pipe_flowType flowtype);
bool pipe_pools(PoolPtr input, PoolPtr out, bool copyInsteadOfMove);

    private:
           
        appManager();   // private constructor
        ~appManager();  // private destructor
    
        appManager(const appManager&) = delete;
        appManager& operator=(const appManager&) = delete;
        WindowManager& ref_wm; //blank reference, we attempt to fill on create of this object
        //create order MUST be windowmanager then appmanager on boot or you're fucked
        
        
        std::vector<std::shared_ptr<AppBase>> apps;
        std::shared_ptr<AppBase> focused_app; //so now we can have the app manager know what we're focused on
        std::unordered_map<std::string, AppFactory> app_factories;
        std::unordered_map<std::string, std::weak_ptr<AppBase>> running_apps; 
    };




