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
    PINNED_TO_CORE      =   1<<10  //PIN TO CORE 1 (core 0 runs spi and wifi and hence should be avoided in 
    //overload, but this adds some potential issues with threading
};

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
const char* get_app_name() const { return cfg_.name; } //getter because we love input handling so much!!!
    explicit AppBase(const ApplicationConfig& cfg);
    virtual ~AppBase();
    void init();
    // Lifecycle methods (override in derived classes)
    virtual void tick_app(uint32_t delta_ms) = 0;          // called periodically
    virtual void receive_event_input(const void* event) = 0; // input event
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

    // Window access
    std::shared_ptr<Window> get_window() const { return window_; }
    int appTickRateHZ; //tick rate of the app in hz
    // Configuration queries
    AppCapabilities get_capabilities() const { return cfg_.capabilities; }
    bool has_capability(AppCapability cap) const {
        return (static_cast<uint32_t>(cfg_.capabilities) & static_cast<uint32_t>(cap)) != 0;
    }

    // Task control
    void start_task();
    void stop_task();

protected:
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



class appManager {
    public:
        static appManager& instance();
    
        void register_app(const std::shared_ptr<AppBase>& app);
        void draw_all();
        void DestroyAllApps();
    

        void set_focused_app(std::shared_ptr<AppBase> app);
        std::shared_ptr<AppBase> get_focused_app() const;
        void route_input_to_focused(const InputEvent& ev);
        



    private:
        appManager();   // private constructor
        ~appManager();  // private destructor
    
        appManager(const appManager&) = delete;
        appManager& operator=(const appManager&) = delete;
        WindowManager& ref_wm; //blank reference, we attempt to fill on create of this object
        //create order MUST be windowmanager then appmanager on boot or you're fucked
    
    private:
        std::vector<std::shared_ptr<AppBase>> apps;
        std::shared_ptr<AppBase> focused_app; //so now we can have the app manager know what we're focused on
    };
