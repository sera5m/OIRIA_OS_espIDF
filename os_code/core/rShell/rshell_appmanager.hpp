#pragma once

#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include "freertos/FreeRTOS.h"
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/streams/rshell_pipe.hpp"
//lol
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/core/notification_sys/rs_notif_dispatcher.h"
#include "os_code/middle_layer/input/hid_t.h"





//forward declarations for other items
struct InputEvent;


// AppManager: Manages the lifecycle of applications, including registration, opening, closing, and inter-app communication.
class appManager {
public:
   // using AppFactory = std::function<std::shared_ptr<AppBase>(const ApplicationConfig&)>;

    static appManager& instance();

    // Registration
    void register_app(const AppManifest& manifest);
    void register_dynamic_app(const AppManifest& manifest);

    std::shared_ptr<AppBase> open_app(const std::string& name);
    std::shared_ptr<AppBase> get_app_by_name(const std::string& name);

    void pause_app(const std::string& name);
    void resume_app(const std::string& name);
    void kill_app(const std::string& name);
    void force_kill_app(const std::string& name);
    void draw_all();
    void DestroyAllApps();


    //state management: focused apps
    void set_focused_app(std::shared_ptr<AppBase> app);
    std::shared_ptr<AppBase> get_focused_app() const;
    void route_input_to_focused(const InputEvent& ev);


   std::shared_ptr<DataPool> establish_pool(size_t bytes, e_type_storage stype);
    bool create_pipe(std::shared_ptr<AppBase> source, const std::vector<PipeTarget>& targets, Rshell_pipe_flowType flow);
    void force_check_pools();
    void close_current_and_open(const std::string& name);
    void swap_to_app(std::shared_ptr<AppBase> new_app);
    bool is_app_running(const std::string& name);
    bool establish_outlet(std::shared_ptr<AppBase> app, size_t bytes, e_type_storage stype);
    bool establish_inlet(std::shared_ptr<AppBase> app, DataPool* pool, Rshell_pipe_flowType flow);
    void start_manager_task();
    void update();

// In appManager class (public section):
bool is_app_registered(const std::string& name) const;

private:
    appManager();
    ~appManager();

    void cleanup_old_app(std::shared_ptr<AppBase> old_app);
    std::shared_ptr<AppBase> get_app(const std::string& name);

    TaskHandle_t manager_task_ = nullptr;
    WindowManager& ref_wm;

    std::vector<std::shared_ptr<AppBase>> apps;
    std::shared_ptr<AppBase> focused_app;
    std::unordered_map<std::string, AppManifest> app_manifests;  // the registry is here
    std::unordered_map<std::string, std::weak_ptr<AppBase>> running_apps;
    std::vector<std::unique_ptr<RshellPipe>> active_pipes;

    bool pipe_to_apps(std::shared_ptr<AppBase> from,const std::vector<std::shared_ptr<AppBase>>& to_apps,Rshell_pipe_flowType flow);
    bool pipe_to_pools(std::shared_ptr<AppBase> from,const std::vector<DataPool*>& to_pools,Rshell_pipe_flowType flow);
    bool pipe_to_targets(std::shared_ptr<AppBase> from,const std::vector<PipeTarget>& targets,Rshell_pipe_flowType flow);
    bool pipe_apps(std::shared_ptr<AppBase> from, std::shared_ptr<AppBase> to, Rshell_pipe_flowType flow);
    bool connect_pipe(std::shared_ptr<AppBase> source,std::shared_ptr<AppBase> target,bool use_psram_ring);

    appManager(const appManager&) = delete;
    appManager& operator=(const appManager&) = delete;
};


