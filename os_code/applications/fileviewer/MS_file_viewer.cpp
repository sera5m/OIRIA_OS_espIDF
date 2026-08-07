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

extern "C" bool jpeg_decode_file(const char* path, uint16_t* fb, int fb_w, int fb_h) __attribute__((weak));
extern "C" bool bmp_decode_file(const char* path, uint16_t* fb, int fb_w, int fb_h) __attribute__((weak));

static const char* TAG = "File_Viewer_App";

// ===================================================================

File_Viewer_App::File_Viewer_App(const ApplicationConfig& cfg)
    : AppBase(cfg)
{
    appTickRateHZ = 10;
}

void File_Viewer_App::on_start()
{
    ESP_LOGI(TAG, "File Viewer started");

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
            .UpdateRate = 1.0f
        },
        "FileViewer"
    );

    WindowManager::getInstance().registerWindow(fv_app_window);
    bind_main_window(fv_app_window);

    current_path = "/sdcard";
    refresh_directory(current_path);
    on_draw();
}

void File_Viewer_App::on_stop()
{
    if (fv_app_window) {
        WindowManager::getInstance().unregisterWindow(fv_app_window);
        fv_app_window.reset();
    }
    text_buffer.clear();
    directory_entries.clear();
}

void File_Viewer_App::on_pause()  {}
void File_Viewer_App::on_resume() { on_draw(); }

// ===================================================================
// Markup-safe text (prevents tokenizer eating filenames / file bodies)
// ===================================================================

void File_Viewer_App::append_safe(std::string& out, const std::string& s)
{
    for (char c : s) {
        if (c == '<') {
            // Break potential <| sequences; show a stand-in
            out += "{lt}";
        } else if (c == '>') {
            out += "{gt}";
        } else if (c == '\r') {
            continue;
        } else if (c == '\n') {
            out += "<|n|>";
        } else if ((unsigned char)c < 0x20 && c != '\t') {
            out.push_back('?');
        } else {
            out.push_back(c);
        }
    }
}

// ===================================================================
// Path helpers
// ===================================================================

std::string File_Viewer_App::join_path(const std::string& base, const std::string& name) const
{
    if (base.empty() || base.back() == '/') return base + name;
    return base + "/" + name;
}

std::string File_Viewer_App::parent_path(const std::string& path) const
{
    if (path == "/sdcard" || path == "/" || path.empty()) return "/sdcard";
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos || pos == 0) return "/sdcard";
    std::string p = path.substr(0, pos);
    if (p.empty()) p = "/sdcard";
    return p;
}

bool File_Viewer_App::path_is_dir(const std::string& path) const
{
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

// ===================================================================
// Directory listing
// ===================================================================

void File_Viewer_App::refresh_directory(const std::string& path)
{
    directory_entries.clear();
    selected_index = 0;
    scroll_offset  = 0;

    // Synthetic top entries
    directory_entries.push_back(DirEntry{ "[Create]", false, OFV_TXT });
    directory_entries.push_back(DirEntry{ "..", true, OFV_TXT });

    DIR* dir = opendir(path.c_str());
    if (!dir) {
        ESP_LOGW(TAG, "opendir('%s') failed", path.c_str());
        return;
    }

    std::vector<DirEntry> dirs;
    std::vector<DirEntry> files;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const char* n = ent->d_name;
        if (!n || n[0] == '\0') continue;
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;

        std::string full = join_path(path, n);
        DirEntry e;
        e.name = n;

        struct stat st{};
        if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            e.is_dir = true;
            e.type = OFV_TXT;
            dirs.push_back(std::move(e));
        } else {
            e.is_dir = false;
            const char* ext = OFV_GetExtension(n);
            e.type = OFV_GetModeFromExt(ext);
            files.push_back(std::move(e));
        }
    }
    closedir(dir);

    auto by_name = [](const DirEntry& a, const DirEntry& b) {
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    };
    std::sort(dirs.begin(), dirs.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);

    for (auto& d : dirs)  directory_entries.push_back(std::move(d));
    for (auto& f : files)  directory_entries.push_back(std::move(f));

    ESP_LOGI(TAG, "Listed %s – %d entries", path.c_str(), (int)directory_entries.size());
}

// ===================================================================
// Colors / labels
// ===================================================================

uint16_t File_Viewer_App::color_for_type(OFV_Mode m) const
{
    switch (m) {
        case OFV_TXT:              return 0xFFFF;
        case OFV_WEBPAGE:          return 0x07FF;
        case OFV_IMG:              return 0xF81F;
        case OFV_AUDIO:            return 0xFFE0;
        case OFV_VIDEO:            return 0xF800;
        case OFV_STREAMED_AUDIO:   return 0xFD20;
        case OFV_STREAMED_VIDEO:   return 0xF800;
        case OFV_3DOBJ:            return 0x07E0;
        case OFV_XR:               return 0xAFE5;
        case OFV_CANVAS_ANIM_REPLAY: return 0x8410;
        case OFV_RAW:              return 0xAD55;
        case OFV_RSHELL_SPECIAL:   return 0x001F;
        default:                   return 0xC618;
    }
}

const char* File_Viewer_App::label_for_type(OFV_Mode m) const
{
    switch (m) {
        case OFV_TXT: return "TXT";
        case OFV_WEBPAGE: return "WEB";
        case OFV_IMG: return "IMG";
        case OFV_AUDIO: return "AUD";
        case OFV_VIDEO: return "VID";
        case OFV_3DOBJ: return "3D";
        case OFV_RSHELL_SPECIAL: return "RSH";
        case OFV_RAW: return "RAW";
        default: return "???";
    }
}

// ===================================================================
// Text load / save
// ===================================================================

bool File_Viewer_App::load_text_file(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        ESP_LOGE(TAG, "fopen('%s') failed", path.c_str());
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) sz = 0;
    if (sz > 64 * 1024) sz = 64 * 1024;

    text_buffer.assign((size_t)sz, '\0');
    if (sz > 0) {
        size_t n = fread(text_buffer.data(), 1, (size_t)sz, f);
        text_buffer.resize(n);
    }
    fclose(f);
    text_path   = path;
    text_cursor = 0;
    text_view_origin = 0;
    text_dirty  = false;
    ESP_LOGI(TAG, "Loaded text %s (%d bytes)", path.c_str(), (int)text_buffer.size());
    return true;
}

bool File_Viewer_App::save_text_file(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        ESP_LOGE(TAG, "save fopen('%s') failed", path.c_str());
        return false;
    }
    if (!text_buffer.empty())
        fwrite(text_buffer.data(), 1, text_buffer.size(), f);
    fclose(f);
    text_dirty = false;
    ESP_LOGI(TAG, "Saved %s (%d bytes)", path.c_str(), (int)text_buffer.size());
    return true;
}

bool File_Viewer_App::create_file_from_cells()
{
    if (create_name.empty()) create_name = "file";
    const char* ext = kExts[create_ext_index % kExtCount];
    std::string full = join_path(current_path, create_name + ext);

    FILE* f = fopen(full.c_str(), "wb");
    if (!f) {
        ESP_LOGE(TAG, "create failed: %s", full.c_str());
        return false;
    }
    const char* seed = "# new file\n";
    fwrite(seed, 1, strlen(seed), f);
    fclose(f);
    ESP_LOGI(TAG, "Created %s", full.c_str());
    return true;
}

// ===================================================================
// Image
// ===================================================================

bool File_Viewer_App::show_image(const std::string& path)
{
    image_path = path;
    const char* ext = OFV_GetExtension(path.c_str());
    fb_clear(0x0000);

    bool ok = false;
    if (strcasecmp(ext, ".bmp") == 0) {
        if (bmp_decode_file)
            ok = bmp_decode_file(path.c_str(), framebuffer, SCREEN_W, SCREEN_H);
    } else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        if (jpeg_decode_file)
            ok = jpeg_decode_file(path.c_str(), framebuffer, SCREEN_W, SCREEN_H);
    }

    if (!ok) {
        std::string msg =
            "<|size=1|><|color=0xF800|>Image decode unavailable<|n|>";
        append_safe(msg, path);
        msg += "<|n|><|color=0x8888|>Link jpeg/bmp decoder";
        fv_app_window->SetText(msg.c_str());
        fv_app_window->dirty = true;
        return false;
    }

    g_display_dirty = true;
    if (core2TaskHandle) xTaskNotifyGive(core2TaskHandle);
    return true;
}

// ===================================================================
// Open
// ===================================================================

void File_Viewer_App::open_selected()
{
    if (selected_index < 0 || selected_index >= (int)directory_entries.size())
        return;

    const DirEntry& e = directory_entries[selected_index];

    if (e.name == "[Create]") {
        create_cell = FCELL_NAME;
        create_name = "note";
        create_ext_index = 0;
        create_name_cursor = (int)create_name.size() - 1;
        if (create_name_cursor < 0) create_name_cursor = 0;
        current_mode = FV_CREATE;
        return;
    }

    if (e.name == "..") {
        current_path = parent_path(current_path);
        refresh_directory(current_path);
        current_mode = FV_MAIN;
        return;
    }

    std::string full = join_path(current_path, e.name);

    if (e.is_dir) {
        current_path = full;
        refresh_directory(current_path);
        current_mode = FV_MAIN;
        return;
    }

    switch (e.type) {
        case OFV_TXT:
        case OFV_WEBPAGE:
        case OFV_RSHELL_SPECIAL:
            if (load_text_file(full))
                current_mode = FV_VIEW_TEXT;
            break;
        case OFV_IMG:
            current_mode = FV_VIEW_IMAGE;
            show_image(full);
            break;
        default:
            ESP_LOGI(TAG, "No handler for type %d (%s)", (int)e.type, e.name.c_str());
            break;
    }
}

// ===================================================================
// Drawing
// ===================================================================

void File_Viewer_App::draw_browser()
{
    std::string text;
    text.reserve(1024);

    // Fixed size=1 for the whole browser list (size leak was the main bug)
    text += "<|size=1|><|color=0x07FF|>Files  ";
    text += "<|color=0x8410|>";
    append_safe(text, current_path);
    text += "<|n|>";

    int start = scroll_offset;
    int end   = std::min(start + kVisibleRows, (int)directory_entries.size());

    for (int i = start; i < end; ++i) {
        const DirEntry& e = directory_entries[i];
        bool sel = (i == selected_index);

        if (sel)
            text += "<|color=0xFFE0|>";
        else if (e.name == "[Create]")
            text += "<|color=0x07E0|>";
        else if (e.is_dir)
            text += "<|color=0x07E0|>";
        else {
            char cbuf[24];
            snprintf(cbuf, sizeof(cbuf), "<|color=0x%04X|>",
                     (unsigned)color_for_type(e.type));
            text += cbuf;
        }

        text += sel ? "> " : "  ";

        if (e.name == "[Create]") {
            text += "[+] Create file";
        } else if (e.is_dir) {
            text += "[";
            append_safe(text, e.name);
            text += "]";
        } else {
            append_safe(text, e.name);
            text += " ";
            text += label_for_type(e.type);
        }
        text += "<|n|>";
    }

    text += "<|color=0x8888|>UP/DN ENTER  HOLD-ENT=create  BACK";

    fv_app_window->SetText(text.c_str());
    fv_app_window->dirty = true;
}

void File_Viewer_App::draw_text_view()
{
    std::string text;
    text.reserve(kTextWindowChars * 2 + 128);

    text += "<|size=1|><|color=0x07FF|>";
    append_safe(text, text_path);
    if (text_dirty) text += " *";
    text += "<|n|>";

    // Keep cursor in window
    if (text_cursor < text_view_origin)
        text_view_origin = text_cursor;
    if (text_cursor >= text_view_origin + kTextWindowChars)
        text_view_origin = text_cursor - kTextWindowChars + 1;
    if (text_view_origin < 0) text_view_origin = 0;

    int start = text_view_origin;
    int end   = std::min((int)text_buffer.size(), start + kTextWindowChars);

    text += "<|color=0xFFFF|>";

    // Draw window with a box around the cursor character (classic editor style)
    for (int i = start; i < end; ++i) {
        char c = text_buffer[i];
        bool at = (i == text_cursor);

        if (at) text += "<|color=0x0000|><|color=0xFFE0|>";  // highlight via bright bg-ish color
        // MWenv has no true bg-per-glyph; use brackets as the "box"
        if (at) text += "[";

        if (c == '<') text += "{lt}";
        else if (c == '>') text += "{gt}";
        else if (c == '\n') text += (at ? "\\n" : "<|n|>");
        else if (c == '\r') continue;
        else if ((unsigned char)c < 0x20) text.push_back('?');
        else text.push_back(c);

        if (at) {
            text += "]";
            text += "<|color=0xFFFF|>";
        }
    }

    // Cursor past end
    if (text_cursor >= (int)text_buffer.size()) {
        text += "<|color=0xFFE0|>[_]<|color=0xFFFF|>";
    }

    char foot[64];
    snprintf(foot, sizeof(foot),
             "<|n|><|color=0x8888|>pos %d/%d  L/R move  HOLD-ENT=save  BACK",
             text_cursor, (int)text_buffer.size());
    text += foot;

    fv_app_window->SetText(text.c_str());
    fv_app_window->dirty = true;
}

void File_Viewer_App::draw_create()
{
    const char* ext = kExts[create_ext_index % kExtCount];

    auto cell = [&](FV_CreateCell which, const std::string& label, const std::string& value) {
        std::string s;
        if (create_cell == which)
            s += "<|color=0xFFE0|>[";
        else
            s += "<|color=0xAD55|> ";
        s += label;
        s += ":";
        // name cell: box the active character
        if (which == FCELL_NAME && create_cell == FCELL_NAME) {
            for (int i = 0; i < (int)value.size(); ++i) {
                if (i == create_name_cursor) s += "[";
                s.push_back(value[i]);
                if (i == create_name_cursor) s += "]";
            }
            if (value.empty()) s += "[_]";
        } else {
            append_safe(s, value);
        }
        if (create_cell == which) s += "]";
        else s += " ";
        s += "<|n|>";
        return s;
    };

    std::string text = "<|size=1|><|color=0x07FF|>Create file<|n|>";
    text += cell(FCELL_DIR,  "dir ", current_path);
    text += cell(FCELL_NAME, "name", create_name);
    text += cell(FCELL_EXT,  "ext ", ext);

    text += "<|n|><|color=0xFFFF|>=> ";
    append_safe(text, join_path(current_path, create_name + ext));
    text += "<|n|>";
    text += "<|color=0x8888|>L/R=cell  U/D=edit  ENT=add-char  HOLD-ENT=make  BACK";

    fv_app_window->SetText(text.c_str());
    fv_app_window->dirty = true;
}

void File_Viewer_App::on_draw()
{
    if (!fv_app_window) return;
    switch (current_mode) {
        case FV_MAIN:       draw_browser();   break;
        case FV_VIEW_TEXT:  draw_text_view(); break;
        case FV_CREATE:     draw_create();    break;
        case FV_VIEW_IMAGE: {
            std::string cap = "<|size=1|><|color=0xFFFF|>";
            append_safe(cap, image_path);
            cap += "<|n|><|color=0x8888|>BACK=close";
            fv_app_window->SetText(cap.c_str());
            fv_app_window->dirty = true;
            break;
        }
        default:
            draw_browser();
            break;
    }
}

// ===================================================================
// Create cell editing
// ===================================================================

void File_Viewer_App::cycle_name_char(int delta)
{
    if (create_name.empty()) {
        create_name = "a";
        create_name_cursor = 0;
        return;
    }
    if (create_name_cursor < 0) create_name_cursor = 0;
    if (create_name_cursor >= (int)create_name.size())
        create_name_cursor = (int)create_name.size() - 1;

    // Allowed: a-z 0-9 _
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyz0123456789_";
    const int alen = (int)sizeof(alphabet) - 1;

    char& c = create_name[create_name_cursor];
    int idx = 0;
    for (int i = 0; i < alen; ++i) {
        if (alphabet[i] == c) { idx = i; break; }
    }
    idx = (idx + delta) % alen;
    if (idx < 0) idx += alen;
    c = alphabet[idx];
}

void File_Viewer_App::cycle_ext(int delta)
{
    create_ext_index = (create_ext_index + delta) % kExtCount;
    if (create_ext_index < 0) create_ext_index += kExtCount;
}

// ===================================================================
// Tick / input
// ===================================================================

void File_Viewer_App::tick_app(uint32_t delta_ms)
{
    static uint32_t accum = 0;
    accum += delta_ms;
    if (accum >= 400) {
        accum = 0;
        if (current_mode != FV_VIEW_IMAGE)
            on_draw();
    }
}

void File_Viewer_App::receive_event_input(const void* event)
{
    if (!event) return;
    const InputEvent* ev = static_cast<const InputEvent*>(event);

    // ---- Hold ENTER ----
    if (ev->action == KeyAction::Hold && ev->key == KEY_ENTER) {
        if (current_mode == FV_MAIN) {
            create_cell = FCELL_NAME;
            create_name = "note";
            create_ext_index = 0;
            create_name_cursor = (int)create_name.size() - 1;
            current_mode = FV_CREATE;
            on_draw();
            return;
        }
        if (current_mode == FV_VIEW_TEXT) {
            save_text_file(text_path);
            on_draw();
            return;
        }
        if (current_mode == FV_CREATE) {
            if (create_file_from_cells()) {
                refresh_directory(current_path);
                current_mode = FV_MAIN;
                // Select the new file if present
                std::string want = create_name + kExts[create_ext_index % kExtCount];
                for (size_t i = 0; i < directory_entries.size(); ++i) {
                    if (directory_entries[i].name == want) {
                        selected_index = (int)i;
                        break;
                    }
                }
            }
            on_draw();
            return;
        }
    }

    if (ev->action != KeyAction::Tap && ev->action != KeyAction::Hold)
        return;

    switch (current_mode) {

    case FV_MAIN:
        switch (ev->key) {
            case KEY_UP:
                if (selected_index > 0) {
                    selected_index--;
                    if (selected_index < scroll_offset)
                        scroll_offset = selected_index;
                    on_draw();
                }
                break;
            case KEY_DOWN:
                if (selected_index < (int)directory_entries.size() - 1) {
                    selected_index++;
                    if (selected_index >= scroll_offset + kVisibleRows)
                        scroll_offset = selected_index - kVisibleRows + 1;
                    on_draw();
                }
                break;
            case KEY_ENTER:
                open_selected();
                on_draw();
                break;
            case KEY_BACK:
                if (current_path != "/sdcard" && current_path != "/") {
                    current_path = parent_path(current_path);
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
            case KEY_UP:
                // Jump ~one "line" back (to previous \n or window)
                {
                    int i = text_cursor - 1;
                    while (i > 0 && text_buffer[i] != '\n') --i;
                    text_cursor = std::max(0, i);
                    on_draw();
                }
                break;
            case KEY_DOWN:
                {
                    int i = text_cursor;
                    while (i < (int)text_buffer.size() && text_buffer[i] != '\n') ++i;
                    if (i < (int)text_buffer.size()) ++i;
                    text_cursor = i;
                    on_draw();
                }
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
                if (create_cell == FCELL_NAME) cycle_name_char(+1);
                else if (create_cell == FCELL_EXT) cycle_ext(+1);
                // dir cell is read-only (current folder)
                on_draw();
                break;
            case KEY_DOWN:
                if (create_cell == FCELL_NAME) cycle_name_char(-1);
                else if (create_cell == FCELL_EXT) cycle_ext(-1);
                on_draw();
                break;
            case KEY_ENTER:
                // Append a character to the name (+), or advance name cursor
                if (create_cell == FCELL_NAME) {
                    if (ev->action == KeyAction::Hold) {
                        // already handled above
                    } else {
                        // + char
                        if (create_name.size() < 24) {
                            create_name.push_back('a');
                            create_name_cursor = (int)create_name.size() - 1;
                        }
                    }
                }
                on_draw();
                break;
            case KEY_BACK:
                // If name cell and length>1, delete last char (minus); else cancel
                if (create_cell == FCELL_NAME && create_name.size() > 1) {
                    create_name.pop_back();
                    if (create_name_cursor >= (int)create_name.size())
                        create_name_cursor = (int)create_name.size() - 1;
                    on_draw();
                } else {
                    current_mode = FV_MAIN;
                    on_draw();
                }
                break;
            default: break;
        }
        break;

    default:
        break;
    }
}

void register_fileviewer()
{
    AppManifest m;
    m.name = "FileViewerApp";
    m.display_name = "File Viewer";
    m.description = "Browse SD, view text/images, create notes";
    m.capabilities =
        static_cast<uint32_t>(AppCapability::FULLSCREEN) |
        static_cast<uint32_t>(AppCapability::NEEDS_WINDOW) |
        static_cast<uint32_t>(AppCapability::USES_SD_CARD);
    m.stack_size_bytes = 12288;
    m.priority = 5;
    m.tick_rate_hz = 10;
    m.create = [](const ApplicationConfig& cfg) {
        return std::make_shared<File_Viewer_App>(cfg);
    };
    appManager::instance().register_app(m);
}
