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

typedef enum {
    FV_MAIN = 0,          // browsing a directory
    FV_VIEW_TEXT,         // viewing / editing a text file
    FV_VIEW_IMAGE,        // showing a decoded image
    FV_CREATE,            // cell editor: dir | name | ext
    FV_COUNT
} FV_APP_Mode;

// Which cell is active in FV_CREATE
typedef enum {
    FCELL_DIR = 0,
    FCELL_NAME,
    FCELL_EXT,
    FCELL_COUNT
} FV_CreateCell;

struct DirEntry {
    std::string name;     // basename only
    bool is_dir = false;
    OFV_Mode type = OFV_TXT;
};

struct InputEvent;

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

private:
    std::shared_ptr<Window> fv_app_window;

    FV_APP_Mode current_mode = FV_MAIN;

    std::string current_path;
    std::vector<DirEntry> directory_entries;
    int selected_index = 0;
    int scroll_offset  = 0;
    static constexpr int kVisibleRows = 9;

    // Text viewer / editor
    std::string text_path;
    std::string text_buffer;
    int  text_cursor = 0;       // byte index
    int  text_view_origin = 0;  // first visible char (horizontal-ish window)
    bool text_dirty  = false;
    static constexpr int kTextWindowChars = 40;

    // Create-file cells
    FV_CreateCell create_cell = FCELL_NAME;
    std::string   create_name = "note";
    int           create_ext_index = 0;
    int           create_name_cursor = 0;  // which char in name is being edited
    static constexpr const char* kExts[] = {
        ".txt", ".htm", ".md", ".log", ".rgs", ".csv", ".json"
    };
    static constexpr int kExtCount = 7;

    std::string image_path;

    void refresh_directory(const std::string& path);
    bool path_is_dir(const std::string& path) const;
    std::string join_path(const std::string& base, const std::string& name) const;
    std::string parent_path(const std::string& path) const;

    bool load_text_file(const std::string& path);
    bool save_text_file(const std::string& path);
    bool create_file_from_cells();
    bool show_image(const std::string& path);

    uint16_t color_for_type(OFV_Mode m) const;
    const char* label_for_type(OFV_Mode m) const;
    void draw_browser();
    void draw_text_view();
    void draw_create();
    void open_selected();

    // Safe append for MWenv markup (never emit raw "<|")
    static void append_safe(std::string& out, const std::string& s);
    void cycle_name_char(int delta);
    void cycle_ext(int delta);
};

void register_fileviewer();
