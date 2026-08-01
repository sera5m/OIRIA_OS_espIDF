#pragma once
#include <stdint.h>
#include <string>
#include <memory>
#include <vector>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "os_code/core/rShell/rshell_appFramework.hpp"
#include "os_code/core/rShell/rshell_appmanager.hpp"
#include "os_code/core/window_env/MWenv.hpp"
#include "os_code/middle_layer/input/hid_t.h"
#include "hardware/drivers/sd_card/d_sdc.h"

// App-level navigation state
typedef enum {
    FV_MAIN = 0,          // browsing a directory
    FV_VIEW_TEXT,         // viewing / editing a text file
    FV_VIEW_IMAGE,        // showing a decoded image
    FV_CREATE_TXT,        // naming a new .txt file
    FV_COUNT
} FV_APP_Mode;

struct DirEntry {
    std::string name;     // basename only
    bool is_dir = false;
    OFV_Mode type = OFV_TXT;
};

struct InputEvent;  // forward

class File_Viewer_App : public AppBase {
public:
    explicit File_Viewer_App(const ApplicationConfig& cfg);

    void tick_app(uint32_t delta_ms) override;
    void receive_event_input(const void* event) override;
    void on_draw() override;

    void on_start() override;
    void on_stop() override;
    void on_pause() override;
    void on_resume() override;

    void suspend();
    void force_close();

private:
    std::shared_ptr<Window> fv_app_window;

    FV_APP_Mode current_mode = FV_MAIN;

    std::string current_path;                 // e.g. "/sdcard" or "/sdcard/notes"
    std::vector<DirEntry> directory_entries;
    int selected_index = 0;
    int scroll_offset  = 0;
    static constexpr int kVisibleRows = 10;

    // Text viewer / editor
    std::string text_path;
    std::string text_buffer;                  // full file in PSRAM-friendly std::string for now
    int  text_cursor = 0;                     // byte index for simple insert
    bool text_dirty  = false;

    // Create-file name buffer
    std::string new_name_buf;

    // Image path currently shown (decoded straight into framebuffer)
    std::string image_path;

    // Directory
    void refresh_directory(const std::string& path);
    bool path_is_dir(const std::string& path) const;
    std::string join_path(const std::string& base, const std::string& name) const;
    std::string parent_path(const std::string& path) const;

    // File ops
    bool load_text_file(const std::string& path);
    bool save_text_file(const std::string& path);
    bool create_text_file(const std::string& path);
    bool show_image(const std::string& path);   // decode JPEG/BMP → framebuffer

    // UI helpers
    uint16_t color_for_type(OFV_Mode m) const;
    const char* label_for_type(OFV_Mode m) const;
    void draw_browser();
    void draw_text_view();
    void draw_create_txt();
    void open_selected();
};

void register_fileviewer();
