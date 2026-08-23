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
    FV_MAIN = 0,
    FV_VIEW_TEXT,
    FV_VIEW_IMAGE,
    FV_CREATE,
    FV_SEARCH,       // filter directory list
    FV_DIAL_EDIT,    // generic dial text entry overlay
    FV_COUNT
} FV_APP_Mode;

typedef enum {
    FCELL_DIR = 0,
    FCELL_NAME,
    FCELL_EXT,
    FCELL_COUNT
} FV_CreateCell;

// What the dial editor is filling
typedef enum {
    DIAL_TARGET_CREATE_NAME = 0,
    DIAL_TARGET_SEARCH,
    DIAL_TARGET_TEXT_INSERT
} DialTarget;

struct DirEntry {
    std::string name;
    bool is_dir = false;
    OFV_Mode type = OFV_TXT;
    int depth = 0;   // breadcrumb / tree indent
};

struct InputEvent;

// ---------------------------------------------------------------------------
// Dial text entry
//
// Display (monospace alignment):
//   [-hello_+[ok][x]]
//    -----^
//
// Slot layout (horizontal index):
//   0              = [-]  delete last char (ENTER)
//   1 .. N         = text characters (UP/DOWN cycle glyph)
//   N+1            = [+]  append new char, jump cursor there
//   N+2            = [ok] submit string
//   N+3            = [x]  cancel / exit editor
//
// LEFT/RIGHT  – move pointer
// UP/DOWN     – cycle character under pointer (letters/digits/_)
// ENTER       – confirm current slot action; on a letter, also advance
// BACK        – exit editor without commit (same as [x])
// Triple-ENTER quickly on any slot also submits (ok)
// ---------------------------------------------------------------------------
struct DialEditor {
    std::string text;
    int  pointer = 1;          // 0 = del, 1..len = chars, len+1 = plus, …
    int  char_index = 0;       // which glyph table index when editing a cell
    bool active = false;
    DialTarget target = DIAL_TARGET_CREATE_NAME;

    // Triple-enter detect
    uint32_t last_enter_ms = 0;
    int      enter_burst = 0;

    void open(DialTarget t, const std::string& seed = "");
    void close();
    int  slot_count() const;           // del + chars + plus + ok + x
    int  text_slot_begin() const { return 1; }
    int  text_slot_end() const { return 1 + (int)text.size(); } // exclusive
    int  slot_plus() const { return 1 + (int)text.size(); }
    int  slot_ok() const { return 2 + (int)text.size(); }
    int  slot_x() const { return 3 + (int)text.size(); }

    // Returns true if editor requests commit (caller reads .text)
    // Returns false if still editing; cancelled sets active=false and text unchanged commit flag
    enum class Result { None, Commit, Cancel };
    Result handle_key(int key, bool hold, uint32_t now_ms);
    void   cycle_char(int delta);
    void   render(std::string& out) const;  // appends markup lines
};

class File_Viewer_App : public AppBase {
public:
    explicit File_Viewer_App(const ApplicationConfig& cfg) : AppBase(cfg) {
        appTickRateHZ = 12;
    }

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
    std::vector<DirEntry> filtered_entries;  // search view
    int selected_index = 0;
    int scroll_offset  = 0;
    static constexpr int kVisibleRows = 8;

    std::string text_path;
    std::string text_buffer;
    int  text_cursor = 0;
    int  text_view_origin = 0;
    bool text_dirty  = false;
    static constexpr int kTextWindowChars = 36;

    FV_CreateCell create_cell = FCELL_NAME;
    std::string   create_name = "note";
    int           create_ext_index = 0;
    static constexpr const char* kExts[] = {
        ".txt", ".htm", ".md", ".log", ".rgs", ".csv", ".json"
    };
    static constexpr int kExtCount = 7;

    std::string image_path;
    std::string search_query;
    DialEditor  dial;

    void refresh_directory(const std::string& path);
    bool ensure_sd_mounted();   // sd_remount() if /sdcard not usable
    bool path_is_dir(const std::string& path) const;
    std::string join_path(const std::string& base, const std::string& name) const;
    std::string parent_path(const std::string& path) const;
    bool sd_ready = false;
    bool initial_list_done = false;  // first SD scan deferred to tick_app

    bool load_text_file(const std::string& path);
    bool save_text_file(const std::string& path);
    bool create_file_from_cells();
    bool show_image(const std::string& path);

    uint16_t color_for_type(OFV_Mode m) const;
    const char* label_for_type(OFV_Mode m) const;
    void draw_browser();
    void draw_text_view();
    void draw_create();
    void draw_search();
    void draw_dial_overlay();
    void open_selected();
    void apply_filter();
    const std::vector<DirEntry>& active_list() const;

    static void append_safe(std::string& out, const std::string& s);
    void cycle_ext(int delta);
    void on_dial_commit();
};

void register_fileviewer();