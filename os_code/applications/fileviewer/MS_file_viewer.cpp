#include "MS_file_viewer.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include "os_code/middle_layer/input/input_handler.hpp"
#include "os_code/middle_layer/input/inputProscessorTask/ipt_x.hpp"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"

// Provided by hardware/drivers/sd_card (d_sdc)
extern "C" esp_err_t sd_unmount(void);
extern "C" esp_err_t sd_remount(void);

extern "C" bool jpeg_decode_file(const char* path, uint16_t* fb, int fb_w, int fb_h) __attribute__((weak));
extern "C" bool bmp_decode_file(const char* path, uint16_t* fb, int fb_w, int fb_h) __attribute__((weak));

static const char* TAG = "File_Viewer_App";

static const char kGlyphs[] =
    "abcdefghijklmnopqrstuvwxyz0123456789_-.";
static constexpr int kGlyphCount = (int)sizeof(kGlyphs) - 1;

// ===================================================================
// DialEditor
// ===================================================================

void DialEditor::open(DialTarget t, const std::string& seed) {
    target = t;
    text = seed;
    pointer = text.empty() ? slot_plus() : 1;
    char_index = 0;
    active = true;
    enter_burst = 0;
    last_enter_ms = 0;
}

void DialEditor::close() { active = false; }

int DialEditor::slot_count() const {
    return 1 + (int)text.size() + 1 + 1 + 1;
}

void DialEditor::cycle_char(int delta) {
    if (pointer < text_slot_begin() || pointer >= text_slot_end()) return;
    int ti = pointer - 1;
    if (ti < 0 || ti >= (int)text.size()) return;
    int idx = 0;
    for (int i = 0; i < kGlyphCount; ++i)
        if (kGlyphs[i] == text[ti]) { idx = i; break; }
    idx = (idx + delta) % kGlyphCount;
    if (idx < 0) idx += kGlyphCount;
    text[ti] = kGlyphs[idx];
    char_index = idx;
}

DialEditor::Result DialEditor::handle_key(int key, bool hold, uint32_t now_ms) {
    if (!active) return Result::None;
    if (key == KEY_BACK) { close(); return Result::Cancel; }
    if (key == KEY_LEFT)  { if (pointer > 0) pointer--; return Result::None; }
    if (key == KEY_RIGHT) { if (pointer < slot_count() - 1) pointer++; return Result::None; }
    if (key == KEY_UP)    { cycle_char(+1); return Result::None; }
    if (key == KEY_DOWN)  { cycle_char(-1); return Result::None; }

    if (key == KEY_ENTER) {
        if (now_ms - last_enter_ms < 400) enter_burst++;
        else enter_burst = 1;
        last_enter_ms = now_ms;
        if (enter_burst >= 3) { close(); return Result::Commit; }

        if (pointer == 0) {
            if (!text.empty()) {
                text.pop_back();
                if (pointer > slot_count() - 1) pointer = slot_count() - 1;
            }
            return Result::None;
        }
        if (pointer >= text_slot_begin() && pointer < text_slot_end()) {
            if (pointer < text_slot_end() - 1) pointer++;
            else pointer = slot_plus();
            return Result::None;
        }
        if (pointer == slot_plus()) {
            if (text.size() < 32) {
                text.push_back('a');
                pointer = text_slot_end() - 1;
            }
            return Result::None;
        }
        if (pointer == slot_ok()) { close(); return Result::Commit; }
        if (pointer == slot_x())  { close(); return Result::Cancel; }
    }
    (void)hold;
    return Result::None;
}

void DialEditor::render(std::string& out) const {
    out += "<|size=1|><|color=0xFFFF|>";
    for (int s = 0; s < slot_count(); ++s) {
        bool sel = (s == pointer);
        out += sel ? "<|color=0xFFE0|>" : "<|color=0xAD55|>";
        if (s == 0) {
            out += sel ? "[-]" : "-";
        } else if (s >= text_slot_begin() && s < text_slot_end()) {
            char c = text[s - 1];
            if (sel) { out += "["; out.push_back(c); out += "]"; }
            else out.push_back(c);
        } else if (s == slot_plus()) {
            out += sel ? "[+]" : "+";
        } else if (s == slot_ok()) {
            out += sel ? "[ok]" : "ok";
        } else if (s == slot_x()) {
            out += sel ? "[x]" : "x";
        }
    }
    out += "<|n|><|color=0x07FF|>";
    for (int s = 0; s < pointer; ++s) {
        if (s == 0) out += "  ";
        else if (s >= text_slot_begin() && s < text_slot_end()) out += " ";
        else if (s == slot_plus()) out += "  ";
        else out += "   ";
    }
    out += "^<|n|><|color=0x8888|>L/R pos  U/D char  ENT act  3xENT=ok  BACK=x<|n|>";
}

// ===================================================================

File_Viewer_App::File_Viewer_App(const ApplicationConfig& cfg) : AppBase(cfg) {
    appTickRateHZ = 12;
}

bool File_Viewer_App::ensure_sd_mounted() {
    // Fast path: already usable
    DIR* probe = opendir("/sdcard");
    if (probe) {
        closedir(probe);
        sd_ready = true;
        return true;
    }

    ESP_LOGW(TAG, "/sdcard not openable – attempting sd_remount()");
    // Best-effort: unmount stale state then remount
    (void)sd_unmount();
    esp_err_t err = sd_remount();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sd_remount failed: %s", esp_err_to_name(err));
        sd_ready = false;
        return false;
    }

    probe = opendir("/sdcard");
    if (probe) {
        closedir(probe);
        sd_ready = true;
        ESP_LOGI(TAG, "SD remounted OK");
        return true;
    }
    sd_ready = false;
    ESP_LOGE(TAG, "SD remount returned OK but /sdcard still unreadable");
    return false;
}

void File_Viewer_App::on_start() {
    ESP_LOGI(TAG, "File Viewer started");
    // Lean startup: window + placeholder only. SD mount/list runs on first tick
    // so a stack spike cannot corrupt appManager while open_app is still on
    // the input task stack.
    fv_app_window = std::make_shared<Window>(
        WindowCfg{
            .Posx = 0, .Posy = 0, .Layer = 0, .renderPriority = 0,
            .win_width  = static_cast<uint16_t>(v_env.clamped_screen_dim_w),
            .win_height = static_cast<uint16_t>(v_env.clamped_screen_dim_h),
            .win_rotation = 1,
            .AutoAlignment = false, .WrapText = true,
            .borderless = false, .ShowNameAtTopOfWindow = false,
            .TextSizeMult = 1,
            .BorderColor = 0x12FF, .BgColor = 0x0021,
            .Bg_secondaryColor = 0xFF34, .WinTextColor = 0xFFFF,
            .backgroundType = BgFillType::Solid,
            .UpdateRate = 0.5f
        },
        "FileViewer"
    );
    WindowManager::getInstance().registerWindow(fv_app_window);
    bind_main_window(fv_app_window);

    current_path = "/sdcard";
    directory_entries.clear();
    directory_entries.push_back(DirEntry{"[Create]", false, OFV_TXT, 0});
    directory_entries.push_back(DirEntry{"[Search]", false, OFV_TXT, 0});
    directory_entries.push_back(DirEntry{"..", true, OFV_TXT, 0});
    directory_entries.push_back(DirEntry{"(loading...)", false, OFV_RAW, 0});
    apply_filter();
    selected_index = 0;
    scroll_offset = 0;
    sd_ready = false;
    initial_list_done = false;
    on_draw();
}

void File_Viewer_App::on_stop() {
    if (fv_app_window) {
        WindowManager::getInstance().unregisterWindow(fv_app_window);
        fv_app_window.reset();
    }
    text_buffer.clear();
    directory_entries.clear();
    filtered_entries.clear();
    sd_ready = false;
    initial_list_done = false;
}

void File_Viewer_App::on_pause()  {}
void File_Viewer_App::on_resume() { on_draw(); }

void File_Viewer_App::append_safe(std::string& out, const std::string& s) {
    for (char c : s) {
        if (c == '<') out += "{lt}";
        else if (c == '>') out += "{gt}";
        else if (c == '\r') continue;
        else if (c == '\n') out += "<|n|>";
        else if ((unsigned char)c < 0x20 && c != '\t') out.push_back('?');
        else out.push_back(c);
    }
}

std::string File_Viewer_App::join_path(const std::string& base, const std::string& name) const {
    if (base.empty() || base.back() == '/') return base + name;
    return base + "/" + name;
}

std::string File_Viewer_App::parent_path(const std::string& path) const {
    if (path == "/sdcard" || path == "/" || path.empty()) return "/sdcard";
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos || pos == 0) return "/sdcard";
    std::string p = path.substr(0, pos);
    return p.empty() ? "/sdcard" : p;
}

bool File_Viewer_App::path_is_dir(const std::string& path) const {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void File_Viewer_App::refresh_directory(const std::string& path) {
    directory_entries.clear();
    selected_index = 0;
    scroll_offset = 0;
    directory_entries.push_back(DirEntry{"[Create]", false, OFV_TXT, 0});
    directory_entries.push_back(DirEntry{"[Search]", false, OFV_TXT, 0});
    directory_entries.push_back(DirEntry{"..", true, OFV_TXT, 0});

    DIR* dir = opendir(path.c_str());
    if (!dir) {
        // Card may have been ejected / never mounted – try once
        if (ensure_sd_mounted())
            dir = opendir(path.c_str());
    }
    if (!dir) {
        ESP_LOGW(TAG, "opendir('%s') failed (sd_ready=%d)", path.c_str(), (int)sd_ready);
        // Synthetic error row so the UI is not a blank list
        directory_entries.push_back(DirEntry{"! SD not mounted", false, OFV_RAW, 0});
        apply_filter();
        return;
    }

    std::vector<DirEntry> dirs, files;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const char* n = ent->d_name;
        if (!n || !n[0] || strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
        std::string full = join_path(path, n);
        DirEntry e;
        e.name = n;
        e.depth = 1;
        struct stat st{};
        if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            e.is_dir = true;
            dirs.push_back(std::move(e));
        } else {
            e.is_dir = false;
            e.type = OFV_GetModeFromExt(OFV_GetExtension(n));
            files.push_back(std::move(e));
        }
    }
    closedir(dir);
    auto by_name = [](const DirEntry& a, const DirEntry& b) {
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    };
    std::sort(dirs.begin(), dirs.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);
    for (auto& d : dirs) directory_entries.push_back(std::move(d));
    for (auto& f : files) directory_entries.push_back(std::move(f));
    apply_filter();
}

void File_Viewer_App::apply_filter() {
    filtered_entries.clear();
    if (search_query.empty()) {
        filtered_entries = directory_entries;
        return;
    }
    std::string q = search_query;
    for (char& c : q) c = (char)tolower((unsigned char)c);
    for (const auto& e : directory_entries) {
        if (e.name == "[Create]" || e.name == "[Search]" || e.name == "..") {
            filtered_entries.push_back(e);
            continue;
        }
        std::string n = e.name;
        for (char& c : n) c = (char)tolower((unsigned char)c);
        if (n.find(q) != std::string::npos) filtered_entries.push_back(e);
    }
}

const std::vector<DirEntry>& File_Viewer_App::active_list() const {
    return filtered_entries.empty() ? directory_entries : filtered_entries;
}

uint16_t File_Viewer_App::color_for_type(OFV_Mode m) const {
    switch (m) {
        case OFV_TXT: return 0xFFFF;
        case OFV_WEBPAGE: return 0x07FF;
        case OFV_IMG: return 0xF81F;
        case OFV_AUDIO: return 0xFFE0;
        case OFV_VIDEO: return 0xF800;
        case OFV_3DOBJ: return 0x07E0;
        case OFV_RSHELL_SPECIAL: return 0x001F;
        default: return 0xC618;
    }
}

const char* File_Viewer_App::label_for_type(OFV_Mode m) const {
    switch (m) {
        case OFV_TXT: return "TXT";
        case OFV_WEBPAGE: return "WEB";
        case OFV_IMG: return "IMG";
        case OFV_AUDIO: return "AUD";
        case OFV_VIDEO: return "VID";
        case OFV_3DOBJ: return "3D";
        case OFV_RSHELL_SPECIAL: return "RSH";
        default: return "???";
    }
}

bool File_Viewer_App::load_text_file(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) sz = 0;
    if (sz > 64 * 1024) sz = 64 * 1024;
    text_buffer.assign((size_t)sz, '\0');
    if (sz > 0) text_buffer.resize(fread(text_buffer.data(), 1, (size_t)sz, f));
    fclose(f);
    text_path = path;
    text_cursor = 0;
    text_view_origin = 0;
    text_dirty = false;
    return true;
}

bool File_Viewer_App::save_text_file(const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    if (!text_buffer.empty()) fwrite(text_buffer.data(), 1, text_buffer.size(), f);
    fclose(f);
    text_dirty = false;
    return true;
}

bool File_Viewer_App::create_file_from_cells() {
    if (create_name.empty()) create_name = "file";
    const char* ext = kExts[create_ext_index % kExtCount];
    std::string full = join_path(current_path, create_name + ext);
    FILE* f = fopen(full.c_str(), "wb");
    if (!f) return false;
    const char* seed = "# new file\n";
    fwrite(seed, 1, strlen(seed), f);
    fclose(f);
    return true;
}

bool File_Viewer_App::show_image(const std::string& path) {
    image_path = path;
    const char* ext = OFV_GetExtension(path.c_str());
    fb_clear(0x0000);
    bool ok = false;
    if (strcasecmp(ext, ".bmp") == 0 && bmp_decode_file)
        ok = bmp_decode_file(path.c_str(), framebuffer, SCREEN_W, SCREEN_H);
    else if ((strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) && jpeg_decode_file)
        ok = jpeg_decode_file(path.c_str(), framebuffer, SCREEN_W, SCREEN_H);
    if (!ok) {
        std::string msg = "<|size=1|><|color=0xF800|>Image decode unavailable<|n|>";
        append_safe(msg, path);
        fv_app_window->SetText(msg.c_str());
        fv_app_window->dirty = true;
        return false;
    }
    g_display_dirty = true;
    if (core2TaskHandle) xTaskNotifyGive(core2TaskHandle);
    return true;
}

void File_Viewer_App::open_selected() {
    const auto& list = active_list();
    if (selected_index < 0 || selected_index >= (int)list.size()) return;
    const DirEntry& e = list[selected_index];
    if (e.name == "[Create]") {
        create_cell = FCELL_NAME;
        create_name = "note";
        create_ext_index = 0;
        current_mode = FV_CREATE;
        return;
    }
    if (e.name == "[Search]") {
        dial.open(DIAL_TARGET_SEARCH, search_query);
        current_mode = FV_DIAL_EDIT;
        return;
    }
    if (e.name == "..") {
        current_path = parent_path(current_path);
        search_query.clear();
        refresh_directory(current_path);
        current_mode = FV_MAIN;
        return;
    }
    std::string full = join_path(current_path, e.name);
    if (e.is_dir) {
        current_path = full;
        search_query.clear();
        refresh_directory(current_path);
        current_mode = FV_MAIN;
        return;
    }
    switch (e.type) {
        case OFV_TXT: case OFV_WEBPAGE: case OFV_RSHELL_SPECIAL:
            if (load_text_file(full)) current_mode = FV_VIEW_TEXT;
            break;
        case OFV_IMG:
            current_mode = FV_VIEW_IMAGE;
            show_image(full);
            break;
        default: break;
    }
}

void File_Viewer_App::draw_browser() {
    std::string text;
    text.reserve(1200);
    text += "<|size=1|><|color=0x07FF|>SD";
    {
        std::string rest = current_path;
        if (rest.rfind("/sdcard", 0) == 0) rest = rest.substr(7);
        if (rest.empty() || rest == "/") text += "<|color=0x8410|> /";
        else {
            size_t i = 0;
            while (i < rest.size()) {
                if (rest[i] == '/') { ++i; continue; }
                size_t j = i;
                while (j < rest.size() && rest[j] != '/') ++j;
                text += "<|color=0x8410|> / <|color=0xFFFF|>";
                append_safe(text, rest.substr(i, j - i));
                i = j;
            }
        }
    }
    if (!search_query.empty()) {
        text += "<|n|><|color=0xF81F|>filter: ";
        append_safe(text, search_query);
    }
    text += "<|n|><|color=0x5A6B|>---------------------------<|n|>";

    const auto& list = active_list();
    int start = scroll_offset;
    int end = std::min(start + kVisibleRows, (int)list.size());
    for (int i = start; i < end; ++i) {
        const DirEntry& e = list[i];
        bool sel = (i == selected_index);
        if (sel) text += "<|color=0xFFE0|>";
        else if (e.name == "[Create]" || e.name == "[Search]") text += "<|color=0x07E0|>";
        else if (e.is_dir) text += "<|color=0x07E0|>";
        else {
            char cbuf[24];
            snprintf(cbuf, sizeof(cbuf), "<|color=0x%04X|>", (unsigned)color_for_type(e.type));
            text += cbuf;
        }
        text += sel ? ">" : " ";
        if (e.depth > 0) {
            text += " ";
            for (int d = 0; d < e.depth; ++d) text += (d == e.depth - 1) ? "+-" : "| ";
        }
        if (e.name == "[Create]") text += "[+] Create";
        else if (e.name == "[Search]") text += "[?] Search";
        else if (e.is_dir) { text += "["; append_safe(text, e.name); text += "]/"; }
        else { append_safe(text, e.name); text += " "; text += label_for_type(e.type); }
        text += "<|n|>";
    }
    text += "<|color=0x8888|>";
    text += sd_ready ? "SD ok  " : "SD ?  ";
    text += "U/D ENT  L=search  HOLD=create  BACK";
    fv_app_window->SetText(text.c_str());
    fv_app_window->dirty = true;
}

void File_Viewer_App::draw_text_view() {
    std::string text;
    text += "<|size=1|><|color=0x07FF|>";
    append_safe(text, text_path);
    if (text_dirty) text += " *";
    text += "<|n|><|color=0xFFFF|>";
    if (text_cursor < text_view_origin) text_view_origin = text_cursor;
    if (text_cursor >= text_view_origin + kTextWindowChars)
        text_view_origin = text_cursor - kTextWindowChars + 1;
    if (text_view_origin < 0) text_view_origin = 0;
    int start = text_view_origin;
    int end = std::min((int)text_buffer.size(), start + kTextWindowChars);
    for (int i = start; i < end; ++i) {
        char c = text_buffer[i];
        bool at = (i == text_cursor);
        if (at) text += "<|color=0xFFE0|>[";
        if (c == '<') text += "{lt}";
        else if (c == '>') text += "{gt}";
        else if (c == '\n') text += at ? "\\n" : "<|n|>";
        else if ((unsigned char)c < 0x20) text.push_back('?');
        else text.push_back(c);
        if (at) text += "]<|color=0xFFFF|>";
    }
    if (text_cursor >= (int)text_buffer.size())
        text += "<|color=0xFFE0|>[_]<|color=0xFFFF|>";
    char foot[80];
    snprintf(foot, sizeof(foot),
             "<|n|><|color=0x8888|>%d/%d L/R  ENT=type  HOLD=save BACK",
             text_cursor, (int)text_buffer.size());
    text += foot;
    fv_app_window->SetText(text.c_str());
    fv_app_window->dirty = true;
}

void File_Viewer_App::draw_create() {
    const char* ext = kExts[create_ext_index % kExtCount];
    std::string text = "<|size=1|><|color=0x07FF|>Create file<|n|>";
    auto cell = [&](FV_CreateCell which, const char* label, const std::string& value) {
        text += (create_cell == which) ? "<|color=0xFFE0|>[" : "<|color=0xAD55|> ";
        text += label;
        text += " ";
        append_safe(text, value);
        if (create_cell == which) text += "]";
        text += "<|n|>";
    };
    cell(FCELL_DIR,  "dir ", current_path);
    cell(FCELL_NAME, "name", create_name);
    cell(FCELL_EXT,  "ext ", ext);
    text += "<|color=0xFFFF|>=> ";
    append_safe(text, join_path(current_path, create_name + ext));
    text += "<|n|><|color=0x8888|>L/R cell  U/D ext  ENT=type name  HOLD=make BACK";
    fv_app_window->SetText(text.c_str());
    fv_app_window->dirty = true;
}

void File_Viewer_App::draw_search() { draw_browser(); }

void File_Viewer_App::draw_dial_overlay() {
    std::string text = "<|size=1|><|color=0x07FF|>";
    switch (dial.target) {
        case DIAL_TARGET_CREATE_NAME: text += "Name editor<|n|>"; break;
        case DIAL_TARGET_SEARCH:      text += "Search filter<|n|>"; break;
        case DIAL_TARGET_TEXT_INSERT: text += "Insert text<|n|>"; break;
    }
    dial.render(text);
    fv_app_window->SetText(text.c_str());
    fv_app_window->dirty = true;
}

void File_Viewer_App::on_draw() {
    if (!fv_app_window) return;
    if (dial.active || current_mode == FV_DIAL_EDIT) {
        draw_dial_overlay();
        return;
    }
    switch (current_mode) {
        case FV_MAIN:      draw_browser(); break;
        case FV_VIEW_TEXT: draw_text_view(); break;
        case FV_CREATE:    draw_create(); break;
        case FV_SEARCH:    draw_browser(); break;
        case FV_VIEW_IMAGE: {
            std::string cap = "<|size=1|><|color=0xFFFF|>";
            append_safe(cap, image_path);
            cap += "<|n|><|color=0x8888|>BACK=close";
            fv_app_window->SetText(cap.c_str());
            fv_app_window->dirty = true;
            break;
        }
        default: draw_browser(); break;
    }
}

void File_Viewer_App::cycle_ext(int delta) {
    create_ext_index = (create_ext_index + delta) % kExtCount;
    if (create_ext_index < 0) create_ext_index += kExtCount;
}

void File_Viewer_App::on_dial_commit() {
    switch (dial.target) {
        case DIAL_TARGET_CREATE_NAME:
            create_name = dial.text.empty() ? "file" : dial.text;
            current_mode = FV_CREATE;
            break;
        case DIAL_TARGET_SEARCH:
            search_query = dial.text;
            apply_filter();
            selected_index = 0;
            scroll_offset = 0;
            current_mode = FV_MAIN;
            break;
        case DIAL_TARGET_TEXT_INSERT:
            if (!dial.text.empty()) {
                text_buffer.insert((size_t)text_cursor, dial.text);
                text_cursor += (int)dial.text.size();
                text_dirty = true;
            }
            current_mode = FV_VIEW_TEXT;
            break;
    }
}

void File_Viewer_App::tick_app(uint32_t delta_ms) {
    (void)delta_ms;
    if (!initial_list_done && fv_app_window) {
        initial_list_done = true;
        ensure_sd_mounted();
        refresh_directory(current_path);
        on_draw();
    }
    if (fv_app_window) fv_app_window->dirty = true;
}

void File_Viewer_App::receive_event_input(const void* event) {
    if (!event) return;
    const InputEvent* ev = static_cast<const InputEvent*>(event);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    bool hold = (ev->action == KeyAction::Hold);

    if (dial.active || current_mode == FV_DIAL_EDIT) {
        if (ev->action != KeyAction::Tap && !hold) return;
        auto r = dial.handle_key(ev->key, hold, now);
        if (r == DialEditor::Result::Commit) on_dial_commit();
        else if (r == DialEditor::Result::Cancel) {
            if (dial.target == DIAL_TARGET_TEXT_INSERT) current_mode = FV_VIEW_TEXT;
            else if (dial.target == DIAL_TARGET_CREATE_NAME) current_mode = FV_CREATE;
            else current_mode = FV_MAIN;
        }
        on_draw();
        return;
    }

    if (hold && ev->key == KEY_ENTER) {
        if (current_mode == FV_MAIN) {
            create_cell = FCELL_NAME;
            create_name = "note";
            create_ext_index = 0;
            current_mode = FV_CREATE;
            on_draw();
            return;
        }
        if (current_mode == FV_VIEW_TEXT) { save_text_file(text_path); on_draw(); return; }
        if (current_mode == FV_CREATE) {
            if (create_file_from_cells()) {
                refresh_directory(current_path);
                current_mode = FV_MAIN;
            }
            on_draw();
            return;
        }
    }

    if (ev->action != KeyAction::Tap && !hold) return;

    switch (current_mode) {
    case FV_MAIN:
        switch (ev->key) {
            case KEY_UP:
                if (selected_index > 0) {
                    selected_index--;
                    if (selected_index < scroll_offset) scroll_offset = selected_index;
                    on_draw();
                }
                break;
            case KEY_DOWN: {
                const auto& list = active_list();
                if (selected_index < (int)list.size() - 1) {
                    selected_index++;
                    if (selected_index >= scroll_offset + kVisibleRows)
                        scroll_offset = selected_index - kVisibleRows + 1;
                    on_draw();
                }
                break;
            }
            case KEY_LEFT:
                dial.open(DIAL_TARGET_SEARCH, search_query);
                current_mode = FV_DIAL_EDIT;
                on_draw();
                break;
            case KEY_ENTER:
                open_selected();
                on_draw();
                break;
            case KEY_BACK:
                if (current_path != "/sdcard" && current_path != "/") {
                    current_path = parent_path(current_path);
                    search_query.clear();
                    refresh_directory(current_path);
                    on_draw();
                } else {
                    appManager::instance().close_current_and_open("MenuApp");
                }
                break;
            default: break;
        }
        break;

    case FV_VIEW_TEXT:
        switch (ev->key) {
            case KEY_LEFT:
                if (text_cursor > 0) text_cursor--;
                on_draw();
                break;
            case KEY_RIGHT:
                if (text_cursor < (int)text_buffer.size()) text_cursor++;
                on_draw();
                break;
            case KEY_ENTER:
                dial.open(DIAL_TARGET_TEXT_INSERT, "");
                current_mode = FV_DIAL_EDIT;
                on_draw();
                break;
            case KEY_BACK:
                text_buffer.clear();
                text_path.clear();
                current_mode = FV_MAIN;
                on_draw();
                break;
            default: break;
        }
        break;

    case FV_VIEW_IMAGE:
        if (ev->key == KEY_BACK) {
            image_path.clear();
            current_mode = FV_MAIN;
            on_draw();
        }
        break;

    case FV_CREATE:
        switch (ev->key) {
            case KEY_LEFT:
                create_cell = (FV_CreateCell)((create_cell + FCELL_COUNT - 1) % FCELL_COUNT);
                on_draw();
                break;
            case KEY_RIGHT:
                create_cell = (FV_CreateCell)((create_cell + 1) % FCELL_COUNT);
                on_draw();
                break;
            case KEY_UP:
                if (create_cell == FCELL_EXT) cycle_ext(+1);
                on_draw();
                break;
            case KEY_DOWN:
                if (create_cell == FCELL_EXT) cycle_ext(-1);
                on_draw();
                break;
            case KEY_ENTER:
                if (create_cell == FCELL_NAME) {
                    dial.open(DIAL_TARGET_CREATE_NAME, create_name);
                    current_mode = FV_DIAL_EDIT;
                }
                on_draw();
                break;
            case KEY_BACK:
                current_mode = FV_MAIN;
                on_draw();
                break;
            default: break;
        }
        break;

    default:
        break;
    }
}

void register_fileviewer() {
    AppManifest m;
    m.name = "FileViewerApp";
    m.display_name = "File Viewer";
    m.description = "Browse SD, dial text entry, search";
    m.capabilities =
        static_cast<uint32_t>(AppCapability::FULLSCREEN) |
        static_cast<uint32_t>(AppCapability::NEEDS_WINDOW) |
        static_cast<uint32_t>(AppCapability::USES_SD_CARD);
    // SD + path strings + dial editor need headroom; 14K was overflowing into
    // appManager::open_app and corrupting unordered_map hashing.
    m.stack_size_bytes = 32768;  // VFS + dial strings + markup
    m.priority = 5;
    m.tick_rate_hz = 12;
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<File_Viewer_App>(cfg);
    };
    appManager::instance().register_app(m);
}
