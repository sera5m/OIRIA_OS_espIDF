#pragma once

#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/streams/rshell_pipe.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/core/notification_sys/rs_notif_dispatcher.h"
#include "os_code/middle_layer/input/hid_t.h"

struct RegisteredAppInfo {
    std::string name;          // manifest.name  (open_app key)
    std::string display_name;  // shown in UI
    std::string description;
};

struct InputEvent;

// AppManager: lifecycle, registry, pipes, and optional Vulcan / serial teletype.
class appManager {
public:
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

    // Focus / input
    void set_focused_app(std::shared_ptr<AppBase> app);
    std::shared_ptr<AppBase> get_focused_app() const;
    void route_input_to_focused(const InputEvent& ev);

    std::shared_ptr<DataPool> establish_pool(size_t bytes, e_type_storage stype);
    bool create_pipe(std::shared_ptr<AppBase> source,
                     const std::vector<PipeTarget>& targets,
                     Rshell_pipe_flowType flow);
    void force_check_pools();
    // By value — safe after kill_app destroys the previous focused app/menu.
    void close_current_and_open(std::string name);
    void swap_to_app(std::shared_ptr<AppBase> new_app);
    bool is_app_running(const std::string& name);
    bool establish_outlet(std::shared_ptr<AppBase> app, size_t bytes, e_type_storage stype);
    bool establish_inlet(std::shared_ptr<AppBase> app, DataPool* pool, Rshell_pipe_flowType flow);
    void start_manager_task();
    void update();

    bool is_app_registered(const std::string& name) const;
    std::vector<RegisteredAppInfo> list_registered_apps() const;

    // -------------------------------------------------------------------------
    // Vulcan secondary mode (C++ apps stay firmware; .vul is the shell layer)
    // -------------------------------------------------------------------------
    // Run a .vul / .bvul from SD or absolute path via RS-VM (0 = OK).
    int run_vulcan_script(const char* path);

    // Push bytecode/source over collaborative UART (RSDOM_TYPE_VM / VM_SRC).
    int send_vulcan_uart(uint8_t type, const uint8_t* payload, uint16_t len);

    // True if secondary scripting path is enabled (role / env).
    bool vulcan_mode_enabled() const;

    // -------------------------------------------------------------------------
    // Serial-monitor teletype — single .vul/.cvul pipe (one buffer only)
    // -------------------------------------------------------------------------
    // When true, the "terminal" task reads UART0 (idf.py monitor) lines.
    bool isConnectedToSerialMonitor = false;

    void start_serial_terminal();
    void stop_serial_terminal();

    // Load exactly one script into the single pipe buffer and eval it.
    int load_serial_script(const char* path);

    // Feed raw source into the same single buffer (UART line assembly).
    int feed_serial_source(const char* src, size_t len);

private:
    appManager();
    ~appManager();

    void cleanup_old_app(std::shared_ptr<AppBase> old_app);
    std::shared_ptr<AppBase> get_app(const std::string& name);

    TaskHandle_t manager_task_ = nullptr;
    WindowManager& ref_wm;

    std::vector<std::shared_ptr<AppBase>> apps;
    std::shared_ptr<AppBase> focused_app;
    std::unordered_map<std::string, AppManifest> app_manifests;
    std::unordered_map<std::string, std::weak_ptr<AppBase>> running_apps;
    std::vector<std::unique_ptr<RshellPipe>> active_pipes;

    bool pipe_to_apps(std::shared_ptr<AppBase> from,
                      const std::vector<std::shared_ptr<AppBase>>& to_apps,
                      Rshell_pipe_flowType flow);
    bool pipe_to_pools(std::shared_ptr<AppBase> from,
                       const std::vector<DataPool*>& to_pools,
                       Rshell_pipe_flowType flow);
    bool pipe_to_targets(std::shared_ptr<AppBase> from,
                         const std::vector<PipeTarget>& targets,
                         Rshell_pipe_flowType flow);
    bool pipe_apps(std::shared_ptr<AppBase> from, std::shared_ptr<AppBase> to,
                   Rshell_pipe_flowType flow);
    bool connect_pipe(std::shared_ptr<AppBase> source, std::shared_ptr<AppBase> target,
                      bool use_psram_ring);

    // Serial terminal task state (one script slot)
    TaskHandle_t terminal_task_ = nullptr;
    static constexpr size_t kSerialScriptMax = 4096;
    char serial_script_buf_[kSerialScriptMax]{};
    size_t serial_script_len_ = 0;
    bool serial_script_busy_ = false;
    static void terminal_task_fn(void* arg);

    appManager(const appManager&) = delete;
    appManager& operator=(const appManager&) = delete;
};
