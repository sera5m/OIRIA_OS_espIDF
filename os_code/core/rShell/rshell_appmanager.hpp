#pragma once

#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include "freertos/FreeRTOS.h"
#include "os_code/core/notification_sys/rs_notif_dispatcher.h"
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/streams/rshell_pipe.hpp"
#include "os_code/core/rShell/defaultAppList.hpp"
class appManager {
public:
    using AppFactory = std::function<std::shared_ptr<AppBase>()>;

    static appManager& instance();

    // Factory
    void register_app_type(const std::string& name, AppFactory factory);
    std::shared_ptr<AppBase> create_app(const std::string& name);
    std::shared_ptr<AppBase> launch_app(const std::string& name);

    // Lifecycle
    std::shared_ptr<AppBase> open_app(const std::string& name);
    std::shared_ptr<AppBase> get_app_by_name(const std::string& name);
    std::shared_ptr<AppBase> get_app(const std::string& name);

    void close_current_and_open(const std::string& name);
    void swap_to_app(std::shared_ptr<AppBase> new_app);

    void register_app(const std::shared_ptr<AppBase>& app);
    void draw_all();
    void DestroyAllApps();

    void set_focused_app(std::shared_ptr<AppBase> app);
    std::shared_ptr<AppBase> get_focused_app() const;
    void route_input_to_focused(const InputEvent& ev);

    // Linux-style
    void pause_app(const std::string& name);
    void resume_app(const std::string& name);
    void kill_app(const std::string& name);
    void force_kill_app(const std::string& name);

    bool is_app_running(const std::string& name);

    // Pool & Pipe
    std::shared_ptr<DataPool> establish_pool(size_t bytes, e_type_storage stype);
    bool establish_outlet(std::shared_ptr<AppBase> app, size_t bytes, e_type_storage stype);
    bool establish_inlet(std::shared_ptr<AppBase> app, DataPool* pool, Rshell_pipe_flowType flow);

    bool create_pipe(std::shared_ptr<AppBase> source,
                     const std::vector<PipeTarget>& targets,
                     Rshell_pipe_flowType flow);

    void force_check_pools();

    // Maintenance
    void start_manager_task();
    void update();

private:
    appManager();
    ~appManager();

    void cleanup_old_app(std::shared_ptr<AppBase> old_app);

    TaskHandle_t manager_task_ = nullptr;
    WindowManager& ref_wm;

    std::vector<std::shared_ptr<AppBase>> apps;
    std::shared_ptr<AppBase> focused_app;
    std::unordered_map<std::string, AppFactory> app_factories;
    std::unordered_map<std::string, std::weak_ptr<AppBase>> running_apps;



    // Stub pipe methods (add these declarations)
    bool pipe_to_apps(std::shared_ptr<AppBase> from,
                      const std::vector<std::shared_ptr<AppBase>>& to_apps,
                      Rshell_pipe_flowType flow);

    bool pipe_to_pools(std::shared_ptr<AppBase> from,
                       const std::vector<DataPool*>& to_pools,
                       Rshell_pipe_flowType flow);

    bool pipe_to_targets(std::shared_ptr<AppBase> from,
                         const std::vector<PipeTarget>& targets,
                         Rshell_pipe_flowType flow);

    bool pipe_apps(std::shared_ptr<AppBase> from, std::shared_ptr<AppBase> to, Rshell_pipe_flowType flow);

    bool connect_pipe(std::shared_ptr<AppBase> source,
                      std::shared_ptr<AppBase> target,
                      bool use_psram_ring);



    std::vector<std::unique_ptr<RshellPipe>> active_pipes;

    appManager(const appManager&) = delete;
    appManager& operator=(const appManager&) = delete;
};