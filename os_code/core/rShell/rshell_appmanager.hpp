#pragma once

#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <cstddef>
#include <cstdint>
#include <atomic>

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

// More than one failure mode. Ok == 0. Single-mode calls stay bool / pointer.
enum class AppOpStatus : uint8_t {
    Ok = 0,
    NotFound,      // not registered, or not running
    CreateFailed,  // factory or pool alloc returned null
    InvalidArg,    // null app/source, or no targets
};

// Pointer plus reason. `if (!opened)` is true unless status is Ok and app is live.
struct OpenAppResult {
    AppOpStatus status = AppOpStatus::NotFound;
    std::shared_ptr<AppBase> app;
    explicit operator bool() const {
        return status == AppOpStatus::Ok && static_cast<bool>(app);
    }
};

// App manager: process lifecycle, focus, input routing, pipes.
// WindowManager (MWenv) is the compositor: window list, sort, frames.
//
// Ownership:
// - focused_app lives here. Only set_focused_app and unfocus write
//   Window::window_highlighted and the focus raise (layer 255 + sort).
// - The app owns its Window (bind_main_window). This manager only borrows it.
//   A null window is tolerated; bind later re-enters set_focused_app.
// - active_pipes owns each RshellPipe. The pump holds a raw pointer only.
// - focus_epoch_ bumps on each publish. UpdateAll samples it at frame start
//   and acks that sample at frame end. set_focused_app does not wait.
class appManager {
public:
    static appManager& instance();

    // Registration
    void register_app(const AppManifest& manifest);
    void register_dynamic_app(const AppManifest& manifest);

    OpenAppResult open_app(const std::string& name);
    std::shared_ptr<AppBase> get_app_by_name(const std::string& name);

    void pause_app(const std::string& name);
    void resume_app(const std::string& name);
    void kill_app(const std::string& name);
    void force_kill_app(const std::string& name);
    void draw_all();
    void DestroyAllApps();

    // Focus / input. set_focused_app and unfocus are the only highlight writers.
    // Neither waits; focus_app optionally waits for one compositor frame.
    void set_focused_app(std::shared_ptr<AppBase> app);
    void unfocus();
    // NotFound if the app is not running. Ok even if it has no window yet.
    AppOpStatus focus_app(const std::string& name, bool settle = true);
    std::shared_ptr<AppBase> get_focused_app() const;
    void route_input_to_focused(const InputEvent& ev);
    // True once UpdateAll has finished a frame that started at or after the
    // current focus epoch. False on timeout, or if called inside UpdateAll.
    bool wait_focus_settled(uint32_t timeout_ms = 80);

    // bind_main_window calls this. No-op unless `app` is already focused.
    void on_main_window_bound(AppBase* app);

    // Compositor only. Sample before drawing; ack that sample after drawing.
    uint32_t begin_focus_frame();
    void end_focus_frame(uint32_t sampled_epoch);

    // Alloc failure is the only miss — nullptr is the status.
    std::shared_ptr<DataPool> establish_pool(size_t bytes, e_type_storage stype);
    // Primary pipe entry. Kind is chosen from the targets.
    AppOpStatus create_pipe(std::shared_ptr<AppBase> source,
                     const std::vector<PipeTarget>& targets,
                     Rshell_pipe_flowType flow);
    void force_check_pools();
    // By value — safe after kill_app destroys the previous focused app/menu.
    void close_current_and_open(std::string name);
    void swap_to_app(std::shared_ptr<AppBase> new_app);
    bool is_app_running(const std::string& name);
    AppOpStatus establish_outlet(std::shared_ptr<AppBase> app, size_t bytes, e_type_storage stype);
    AppOpStatus establish_inlet(std::shared_ptr<AppBase> app, DataPool* pool, Rshell_pipe_flowType flow);
    void start_manager_task();
    void update();

    bool is_app_registered(const std::string& name) const;
    std::vector<RegisteredAppInfo> list_registered_apps() const;

    // -------------------------------------------------------------------------
    // Toplethos secondary mode (C++ apps stay firmware; .vul is the shell layer)
    // -------------------------------------------------------------------------
    // Run a .vul / .bvul from SD or absolute path via RS-VM (0 = OK).
    int run_toplethos_script(const char* path);

    // Push bytecode/source over collaborative UART (RSDOM_TYPE_VM / VM_SRC).
    int send_toplethos_uart(uint8_t type, const uint8_t* payload, uint16_t len);

    // True if secondary scripting path is enabled (role / env).
    bool toplethos_mode_enabled() const;

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

    // Bumped by set_focused_app / unfocus. Compositor acks a sample, not "latest".
    std::atomic<uint32_t> focus_epoch_{0};
    std::atomic<uint32_t> composited_epoch_{0};
    std::atomic<uint32_t> compositor_depth_{0};
    std::atomic<TaskHandle_t> compositor_task_{nullptr};
    void bump_focus_epoch();

    // Same op as create_pipe, different target kinds. Not separate mechanisms.
    AppOpStatus pipe_to_apps(std::shared_ptr<AppBase> from,
                      const std::vector<std::shared_ptr<AppBase>>& to_apps,
                      Rshell_pipe_flowType flow);
    AppOpStatus pipe_to_pools(std::shared_ptr<AppBase> from,
                       const std::vector<DataPool*>& to_pools,
                       Rshell_pipe_flowType flow);
    AppOpStatus pipe_to_targets(std::shared_ptr<AppBase> from,
                         const std::vector<PipeTarget>& targets,
                         Rshell_pipe_flowType flow);
    AppOpStatus pipe_apps(std::shared_ptr<AppBase> from, std::shared_ptr<AppBase> to,
                   Rshell_pipe_flowType flow);
    // Not a create_pipe wrapper: forces flow=direct, and use_psram_ring
    // forces an InterBuffer ring even when the target is an app.
    AppOpStatus connect_pipe(std::shared_ptr<AppBase> source, std::shared_ptr<AppBase> target,
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
