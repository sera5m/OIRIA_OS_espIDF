#include "MS_file_viewer.hpp"
#include "esp_log.h"
#include <stdint.h>
#include "esp_timer.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include <string>
#include <memory>
#include <algorithm>
#include <variant>
#include "code_stuff/types.h"
#include <math.h>
#include "hardware/wiring/wiring.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/spi_common.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "rom/cache.h"
#include <string.h>
#include "hardware/drivers/abstraction_layers/al_scr.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "os_code/core/window_env/wenv_basicThemes.h"
#include <vector>
#include "../../../hardware/drivers/psram_std/psram_std.hpp"
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "code_stuff/helperfunctions.hpp"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_master.h"
#include "hardware/drivers/sd_card/d_sdc.h"
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/middle_layer/input/hid_t.h"

static const char* TAG = "File_Viewer_App";

// ===================================================================
// Constructor
// ===================================================================

File_Viewer_App::File_Viewer_App(const ApplicationConfig& cfg)
    : AppBase(cfg)
{
    appTickRateHZ = 5;
}

// ===================================================================
// Lifecycle
// ===================================================================

void File_Viewer_App::on_start()
{
    ESP_LOGI(TAG, "File Viewer started – creating window");

    fv_app_window = std::make_shared<Window>(
        WindowCfg{
            .Posx = 0,
            .Posy = 0,
            .Layer = 0,
            .renderPriority = 0,
            .win_width = static_cast<uint16_t>(v_env.clamped_screen_dim_w),
            .win_height = static_cast<uint16_t>(v_env.clamped_screen_dim_h),
            .win_rotation = 1,
            .AutoAlignment = false,
            .WrapText = true,
            .borderless = false,
            .ShowNameAtTopOfWindow = false,
            .TextSizeMult = 1,
            .name = {0},
            .optionsbitmask = 0,
            .BorderColor = 0x12FF,
            .BgColor = 0x0021,
            .Bg_secondaryColor = 0xFF34,
            .WinTextColor = 0xFFFF,
            .backgroundType = BgFillType::Solid,
            .UpdateRate = 1.0f
        },
        "FileViewer"
    );
    
    WindowManager::getInstance().registerWindow(fv_app_window);
    bind_main_window(fv_app_window);
    
    // Initialize with root directory
    current_path = "/sdcard";
    refresh_directory(current_path);
    
    on_draw();
}

void File_Viewer_App::on_stop()
{
    ESP_LOGI(TAG, "File Viewer stopped");
    if (fv_app_window) {
        WindowManager::getInstance().unregisterWindow(fv_app_window);
        fv_app_window.reset();
    }
}

void File_Viewer_App::on_pause()  { ESP_LOGI(TAG, "File Viewer paused"); }
void File_Viewer_App::on_resume() { ESP_LOGI(TAG, "File Viewer resumed"); }

void File_Viewer_App::suspend()
{
    ESP_LOGI(TAG, "File Viewer suspending");
    on_pause();
}

void File_Viewer_App::force_close()
{
    ESP_LOGI(TAG, "File Viewer force close");
    on_stop();
    stop_task();
}

// ===================================================================
// Directory Operations
// ===================================================================

void File_Viewer_App::refresh_directory(const std::string& path)
{
    directory_entries.clear();
    directory_entries.push_back("..");  // Parent directory
    
    // TODO: Implement actual directory reading using d_sdc
    // For now, add some dummy entries
    directory_entries.push_back("file1.txt");
    directory_entries.push_back("file2.rpool");
    directory_entries.push_back("config.rpool");
    directory_entries.push_back("subfolder/");
    
    selected_index = 0;
    scroll_offset = 0;
}

// ===================================================================
// Drawing
// ===================================================================

void File_Viewer_App::on_draw()
{
    if (!fv_app_window) return;

    std::string text = "<|size=3|><|color=0xFFFF|>File Viewer<|n|>";
    text += "<|size=1|><|color=0x8888|>";
    text += "Path: " + current_path + "<|n|><|n|>";
    text += "<|size=2|><|color=0xFFFF|>";
    
    int visible_items = 10;
    int start = scroll_offset;
    int end = std::min(start + visible_items, (int)directory_entries.size());
    
    for (int i = start; i < end; i++) {
        const auto& entry = directory_entries[i];
        if (i == selected_index) {
            text += "<|color=0xFDFC|>> " + entry + "<|color=0xFFFF|><|n|>";
        } else {
            text += "  " + entry + "<|n|>";
        }
    }
    
    text += "<|n|><|size=1|><|color=0x8888|>";
    text += "UP/DOWN=Navigate  ENTER=Open  BACK=Exit";
    
    fv_app_window->SetText(text);
    fv_app_window->dirty = true;
}

// ===================================================================
// App Loop
// ===================================================================

void File_Viewer_App::tick_app(uint32_t delta_ms)
{
    static uint32_t accum = 0;
    accum += delta_ms;
    if (accum >= 500) {
        on_draw();
        accum = 0;
    }
}

// ===================================================================
// Input Handling
// ===================================================================

void File_Viewer_App::receive_event_input(const void* event)
{
    if (!event) return;
    
    const InputEvent* ev = static_cast<const InputEvent*>(event);
    bool needs_redraw = false;

    if (ev->action == KeyAction::Tap) {
        switch (ev->key) {
            case KEY_UP:
                if (selected_index > 0) {
                    selected_index--;
                    if (selected_index < scroll_offset) {
                        scroll_offset = selected_index;
                    }
                    needs_redraw = true;
                }
                break;
                
            case KEY_DOWN:
                if (selected_index < (int)directory_entries.size() - 1) {
                    selected_index++;
                    if (selected_index >= scroll_offset + 10) {
                        scroll_offset = selected_index - 9;
                    }
                    needs_redraw = true;
                }
                break;
                
            case KEY_ENTER:
                if (selected_index < (int)directory_entries.size()) {
                    const std::string& entry = directory_entries[selected_index];
                    if (entry == "..") {
                        // Go up one directory
                        size_t pos = current_path.find_last_of('/');
                        if (pos != std::string::npos && pos > 0) {
                            current_path = current_path.substr(0, pos);
                            if (current_path.empty()) current_path = "/sdcard";
                            refresh_directory(current_path);
                            needs_redraw = true;
                        }
                    } else if (!entry.empty() && entry.back() == '/') {
                        // Enter subdirectory
                        current_path = current_path + "/" + entry.substr(0, entry.length() - 1);
                        refresh_directory(current_path);
                        needs_redraw = true;
                    } else {
                        // Open file (for now, just show a message)
                        ESP_LOGI(TAG, "Opening file: %s", entry.c_str());
                    }
                }
                break;
                
            case KEY_BACK:
                appManager::instance().close_current_and_open("MenuApp");
                break;
        }
    }
    
    if (needs_redraw) {
        on_draw();
    }
}

// ===================================================================
// Pool Operations
// ===================================================================

bool File_Viewer_App::load_rpool(const std::string& path)
{
    ESP_LOGI(TAG, "Loading .rpool: %s", path.c_str());
    // TODO: Implement .rpool loading
    return true;
}

bool File_Viewer_App::save_rpool(const std::string& path)
{
    if (!current_pool) return false;
    ESP_LOGI(TAG, "Saving .rpool: %s (size=%zu)", path.c_str(), current_pool->size());
    return true;
}

// ===================================================================
// Registration
// ===================================================================

static std::shared_ptr<AppBase> create_fileviewer(const ApplicationConfig& cfg) {
    return std::make_shared<File_Viewer_App>(cfg);
}

void register_fileviewer() {
    AppManifest m;
    m.name = "FileViewerApp";
    m.display_name = "File Viewer";
    m.description = "Browse files on SD card";
    m.capabilities = static_cast<uint32_t>(AppCapability::FULLSCREEN) | 
                     static_cast<uint32_t>(AppCapability::NEEDS_WINDOW) |
                     static_cast<uint32_t>(AppCapability::USES_SD_CARD);
    m.stack_size_bytes = 8192;
    m.priority = 5;
    m.tick_rate_hz = 5;
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<File_Viewer_App>(cfg);
    };
    
    appManager::instance().register_app(m);
}