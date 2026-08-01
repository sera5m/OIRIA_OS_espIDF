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
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"

// If you have an ESP JPEG decoder component, expose something like:
//   bool jpeg_decode_file(const char* path, uint16_t* fb, int fb_w, int fb_h);
// and link it. Stub falls back to a placeholder message.
extern "C" bool jpeg_decode_file(const char* path, uint16_t* fb, int fb_w, int fb_h) __attribute__((weak));
extern "C" bool bmp_decode_file(const char* path, uint16_t* fb, int fb_w, int fb_h) __attribute__((weak));

static const char* TAG = "File_Viewer_App";

// ===================================================================
// Constructor / lifecycle
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

void File_Viewer_App::suspend()     { on_pause(); }
void File_Viewer_App::force_close() { on_stop(); stop_task(); }

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
// Directory listing (real VFS)
// ===================================================================

void File_Viewer_App::refresh_directory(const std::string& path)
{
    directory_entries.clear();
    selected_index = 0;
    scroll_offset  = 0;

    // Always offer parent
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

        // Prefer stat – d_type is not always reliable on FAT
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
// Colors / labels by OFV type
// ===================================================================

uint16_t File_Viewer_App::color_for_type(OFV_Mode m) const
{
    switch (m) {
        case OFV_TXT:              return 0xFFFF; // white
        case OFV_WEBPAGE:          return 0x07FF; // cyan
        case OFV_IMG:              return 0xF81F; // magenta
        case OFV_AUDIO:            return 0xFFE0; // yellow
        case OFV_VIDEO:            return 0xF800; // red
        case OFV_STREAMED_AUDIO:   return 0xFD20; // orange
        case OFV_STREAMED_VIDEO:   return 0xF800;
        case OFV_3DOBJ:            return 0x07E0; // green
        case OFV_XR:               return 0xAFE5;
        case OFV_CANVAS_ANIM_REPLAY: return 0x8410;
        case OFV_RAW:              return 0xAD55;
        case OFV_RSHELL_SPECIAL:   return 0x001F; // blue
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
// Text file load / save / create
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
    // Cap at 64 KB for safety on this device
    if (sz > 64 * 1024) sz = 64 * 1024;

    text_buffer.assign((size_t)sz, '\0');
    if (sz > 0) {
        size_t n = fread(text_buffer.data(), 1, (size_t)sz, f);
        text_buffer.resize(n);
    }
    fclose(f);
    text_path   = path;
    text_cursor = (int)text_buffer.size();
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
    if (!text_buffer.empty()) {
        fwrite(text_buffer.data(), 1, text_buffer.size(), f);
    }
    fclose(f);
    text_dirty = false;
    ESP_LOGI(TAG, "Saved %s (%d bytes)", path.c_str(), (int)text_buffer.size());
    return true;
}

bool File_Viewer_App::create_text_file(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const char* seed = "# new file\n";
    fwrite(seed, 1, strlen(seed), f);
    fclose(f);
    return true;
}

// ===================================================================
// Image view – decode from disk into framebuffer (no full RAM copy)
// ===================================================================

bool File_Viewer_App::show_image(const std::string& path)
{
    image_path = path;
    const char* ext = OFV_GetExtension(path.c_str());

    // Clear to black first
    fb_clear(0x0000);

    bool ok = false;
    if (strcasecmp(ext, ".bmp") == 0) {
        if (bmp_decode_file)
            ok = bmp_decode_file(path.c_str(), framebuffer, SCREEN_W, SCREEN_H);
        else
            ESP_LOGW(TAG, "bmp_decode_file not linked");
    } else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        if (jpeg_decode_file)
            ok = jpeg_decode_file(path.c_str(), framebuffer, SCREEN_W, SCREEN_H);
        else
            ESP_LOGW(TAG, "jpeg_decode_file not linked – provide weak override");
    }

    if (!ok) {
        // Fallback message drawn as text so the user still sees something
        std::string msg =
            "<|size=2|><|color=0xF800|>Image decode unavailable<|n|>"
            "<|size=1|>" + path + "<|n|>"
            "<|color=0x8888|>Link jpeg_decode_file / bmp_decode_file";
        fv_app_window->SetText(msg.c_str());
        fv_app_window->dirty = true;
        return false;
    }

    // Mark whole screen dirty so the display task pushes it
    g_display_dirty = true;
    if (core2TaskHandle) xTaskNotifyGive(core2TaskHandle);
    return true;
}

// ===================================================================
// Open selected entry
// ===================================================================

void File_Viewer_App::open_selected()
{
    if (selected_index < 0 || selected_index >= (int)directory_entries.size())
        return;

    const DirEntry& e = directory_entries[selected_index];

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
            if (load_text_file(full)) {
                current_mode = FV_VIEW_TEXT;
            }
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

    text += "<|size=2|><|color=0x07FF|>Files<|n|>";
    text += "<|size=1|><|color=0x8410|>";
    text += current_path;
    text += "<|n|>";

    int start = scroll_offset;
    int end   = std::min(start + kVisibleRows, (int)directory_entries.size());

    for (int i = start; i < end; ++i) {
        const DirEntry& e = directory_entries[i];
        bool sel = (i == selected_index);

        if (sel) text += "<|color=0xFFE0|>";
        else if (e.is_dir) text += "<|color=0x07E0|>";
        else text += "<|color=";
        if (!sel && !e.is_dir) {
            char cbuf[16];
            snprintf(cbuf, sizeof(cbuf), "0x%04X", (unsigned)color_for_type(e.type));
            text += cbuf;
            text += "|>";
        } else if (!sel && e.is_dir) {
            /* already set */
        } else {
            /* selected already set */
        }

        text += sel ? "> " : "  ";

        if (e.is_dir) {
            text += "[";
            text += e.name;
            text += "]";
        } else {
            text += e.name;
            text += " <|size=1|>";
            text += label_for_type(e.type);
            text += "<|size=2|>";
        }
        text += "<|n|>";
    }

    text += "<|size=1|><|color=0x8888|>";
    text += "UP/DN  ENTER=open  HOLD-ENT=new.txt  BACK=up/exit";

    fv_app_window->SetText(text.c_str());
    fv_app_window->dirty = true;
}

void File_Viewer_App::draw_text_view()
{
    std::string text;
    text.reserve(text_buffer.size() + 128);

    text += "<|size=1|><|color=0x07FF|>";
    text += text_path;
    if (text_dirty) text += " *";
    text += "<|n|><|color=0xFFFF|>";

    // Show a window of the buffer (simple – full small files)
    // Escape '<' so markup parser doesn't choke on source code
    for (char c : text_buffer) {
        if (c == '<') text += "<<";
        else text += c;
    }

    text += "<|n|><|color=0x8888|>HOLD-ENT=save  BACK=close";

    fv_app_window->SetText(text.c_str());
    fv_app_window->dirty = true;
}

void File_Viewer_App::draw_create_txt()
{
    std::string text =
        "<|size=2|><|color=0xFFFF|>New text file<|n|>"
        "<|size=2|><|color=0xFDFC|>" + new_name_buf + "_.txt<|n|>"
        "<|size=1|><|color=0x8888|>"
        "UP/DN=char  ENTER=next letter  HOLD-ENT=create  BACK=cancel";
    fv_app_window->SetText(text.c_str());
    fv_app_window->dirty = true;
}

void File_Viewer_App::on_draw()
{
    if (!fv_app_window) return;

    switch (current_mode) {
        case FV_MAIN:       draw_browser();   break;
        case FV_VIEW_TEXT:  draw_text_view(); break;
        case FV_CREATE_TXT: draw_create_txt(); break;
        case FV_VIEW_IMAGE:
            // Image already blitted to framebuffer; keep a thin caption
            {
                std::string cap =
                    "<|size=1|><|color=0xFFFF|>" + image_path +
                    "<|n|><|color=0x8888|>BACK=close";
                fv_app_window->SetText(cap.c_str());
                fv_app_window->dirty = true;
            }
            break;
        default:
            draw_browser();
            break;
    }
}

// ===================================================================
// Tick
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

// ===================================================================
// Input
// ===================================================================

void File_Viewer_App::receive_event_input(const void* event)
{
    if (!event) return;
    const InputEvent* ev = static_cast<const InputEvent*>(event);

    // ---- Hold ENTER ----
    if (ev->action == KeyAction::Hold && ev->key == KEY_ENTER) {
        if (current_mode == FV_MAIN) {
            // Start create-txt flow
            new_name_buf = "note";
            current_mode = FV_CREATE_TXT;
            on_draw();
            return;
        }
        if (current_mode == FV_VIEW_TEXT) {
            save_text_file(text_path);
            on_draw();
            return;
        }
        if (current_mode == FV_CREATE_TXT) {
            std::string full = join_path(current_path, new_name_buf + ".txt");
            if (create_text_file(full)) {
                refresh_directory(current_path);
                current_mode = FV_MAIN;
                // Jump selection to the new file if present
                for (size_t i = 0; i < directory_entries.size(); ++i) {
                    if (directory_entries[i].name == new_name_buf + ".txt") {
                        selected_index = (int)i;
                        break;
                    }
                }
            }
            on_draw();
            return;
        }
    }

    if (ev->action != KeyAction::Tap) return;

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
        if (ev->key == KEY_BACK) {
            text_buffer.clear();
            text_path.clear();
            current_mode = FV_MAIN;
            on_draw();
        }
        // Simple text editing can be expanded later (insert char on encoder, etc.)
        break;

    case FV_VIEW_IMAGE:
        if (ev->key == KEY_BACK) {
            image_path.clear();
            current_mode = FV_MAIN;
            on_draw();
        }
        break;

    case FV_CREATE_TXT:
        switch (ev->key) {
            case KEY_UP: {
                // Cycle last character A-Z / 0-9 / _
                if (new_name_buf.empty()) new_name_buf = "a";
                char& c = new_name_buf.back();
                if (c >= 'a' && c < 'z') c++;
                else if (c == 'z') c = '0';
                else if (c >= '0' && c < '9') c++;
                else if (c == '9') c = '_';
                else c = 'a';
                on_draw();
                break;
            }
            case KEY_DOWN: {
                if (new_name_buf.empty()) new_name_buf = "a";
                char& c = new_name_buf.back();
                if (c > 'a' && c <= 'z') c--;
                else if (c == 'a') c = '_';
                else if (c == '_') c = '9';
                else if (c > '0' && c <= '9') c--;
                else c = 'z';
                on_draw();
                break;
            }
            case KEY_ENTER:
                // Append a new letter to the name
                if (new_name_buf.size() < 24) new_name_buf.push_back('a');
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

// ===================================================================
// Registration
// ===================================================================

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
