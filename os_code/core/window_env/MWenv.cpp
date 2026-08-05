
#include <stdint.h>
#include <string>
#include <memory>
#include <sstream>
#include <algorithm>
#include <variant>
#include <vector>
#include <atomic>
#include <string_view>

#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "code_stuff/types.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
#include "hardware/drivers/lcd/st7789v2/t_shapes.h"
#include "hardware/drivers/abstraction_layers/al_scr.h"
#include "hardware/drivers/psram_std/psram_std.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "MWenv.hpp"
#include "os_code/core/window_env/wenv_basicThemes.h"

#include "os_code/core/window_env/AnimWorld.hpp"


#include "PsramBackgroundTile.hpp"
#include "Canvas.hpp"
#include <math.h>
#include <string.h>
#include <algorithm>
#include <string_view>

#include "esp_task_wdt.h"
#include "hardware/wiring/wiring.h"


static const char *TAG = "MWenv";

TaskHandle_t core2TaskHandle = NULL;

 volatile bool g_display_dirty=0;           // set by WindowManager
 volatile uint32_t g_last_display_time=0;
 SemaphoreHandle_t g_display_mutex=NULL;  

toolbarconfig g_defaultToolbarConfig = {
    .tb_overlay = false,
    .tb_update_hz = 2,
    .tb_rot = 1,
    .showToolbar = true,
    .disableTouch = false,
    .expandsDownOnTap = false,
    .ref_iconptrs = {nullptr},
    .icons_shown = static_cast<toolbar_items_t>(0),
    .color = 0x2104
};


std::shared_ptr<Canvas> Window::AddCanvas(const CanvasCfg& cfg) {
    // Create canvas with this window as parent
    CanvasCfg canvasCfg = cfg;
    canvasCfg.parentWindow = this;
    
    m_canvas = Canvas::Create(canvasCfg);
    if (m_canvas) {
        dirty = true;
        ESP_LOGI(TAG, "Canvas added to window");
    }
    return m_canvas;
}

void Window::RemoveCanvas() {
    m_canvas.reset();
    dirty = true;
}

void Window::DrawCanvas() {
    if (m_canvas) {
        m_canvas->Draw();
    }
}
////=================================

Window::~Window() {
    ESP_LOGI(TAG, "Window '%s' destructor called", Currentcfg.name);

    // === Critical: Clean up heavy resources first ===
    ClearText();                    // clears content + cachedChunks + isTokenized

    // Reset tokenized state explicitly
    isTokenized = false;
    if (last_content.capacity() > 0) last_content.clear();  // if you added last_content

    // Canvas
    if (m_canvas) {
        m_canvas->ClearShapes();
        RemoveCanvas();  // or just let shared_ptr die
    }

    // Background tile (PSRAM)
    if (bgTile) {
        bgTile.reset();  // explicit, though shared_ptr would handle it
    }

    // Mark as dead so WindowManager can prune it safely
    IsWindowShown = false;
    dirty = false;
if (text_mtx) { vSemaphoreDelete(text_mtx); text_mtx = nullptr; }

    ESP_LOGI(TAG, "Window '%s' fully cleaned up", Currentcfg.name);
    
}

void Window::setupBackgroundTile() {
    if (!bgTile) {
        bgTile = std::make_shared<PsramBackgroundTile>(32, 32);
    }

    // Only regenerate if something actually changed
    if (bgTile->pbt_cfg.fill_type != win_backgroundpattern ||
        bgTile->primaryColor != bgPrimaryColor ||
        bgTile->secondaryColor != bgSecondaryColor) {

        bgTile->pbt_cfg.fill_type = win_backgroundpattern;
        bgTile->generate_pattern(win_backgroundpattern, bgPrimaryColor, bgSecondaryColor);

        // Update last-known values
        lastBackgroundPattern = win_backgroundpattern;
        lastPrimaryColor      = bgPrimaryColor;
        lastSecondaryColor    = bgSecondaryColor;

        ESP_LOGI(TAG, "Background tile regenerated: pattern %d, primary=0x%04X, secondary=0x%04X",
                 (int)win_backgroundpattern, bgPrimaryColor, bgSecondaryColor);
    }
}






void Window::calculateLogicalDimensions()
{
    const int rot   = wi_sizing.rotation & 3;
    const int rawW  = wi_sizing.Width;
    const int rawH  = wi_sizing.Height;

    // Same logic as in WinDraw()
    logicalW = (rot % 2 == 0) ? rawW : rawH;
    logicalH = (rot % 2 == 0) ? rawH : rawW;
}



Window::Window(const WindowCfg& cfg, const std::string& initialContent)
    : content(stdpsram::String(initialContent.begin(), initialContent.end())),
      Initialcfg(cfg),
      Currentcfg(cfg),
      w_font_info(ft_AVR_classic_6x8)   //init with default font
{
    text_mtx = xSemaphoreCreateMutex();
    // Make sure name is null-terminated
    Currentcfg.name[sizeof(Currentcfg.name) - 1] = '\0';


	bool enable_refresh_override=0;
		
    // Sync sizing & colors
    wi_sizing.Xpos     = Currentcfg.Posx;
    wi_sizing.Ypos     = Currentcfg.Posy;
    wi_sizing.Width    = Currentcfg.win_width;
    wi_sizing.Height   = Currentcfg.win_height;
    wi_sizing.rotation = Currentcfg.win_rotation;

    win_internal_color_background = Currentcfg.BgColor;
    win_internal_color_border     = Currentcfg.BorderColor;
    win_internal_color_text       = Currentcfg.WinTextColor;
    win_internal_textsize_mult    = Currentcfg.TextSizeMult;

    UpdateTickRate          = Currentcfg.UpdateRate;
    win_internal_optionsBitmask = Currentcfg.optionsbitmask;

    // Background pattern and colors
    win_backgroundpattern   = cfg.backgroundType;
    bgPrimaryColor          = win_internal_color_background;
    bgSecondaryColor        = Currentcfg.Bg_secondaryColor;
    //fontdata w_font_info=ft_AVR_classic_6x8; //we'll just set this here as a default... 
    // Create small fixed-size tile (32×32 is perfect for repeating patterns)
    bgTile = std::make_shared<PsramBackgroundTile>(32, 32);

   
    calculateLogicalDimensions();
}



void Window::set_position(uint16_t x, uint16_t y, bool interpolate) {
    if (wi_sizing.Xpos == x && wi_sizing.Ypos == y) return;
    
    wi_sizing.Xpos = x;
    wi_sizing.Ypos = y;
    
    // TODO: Add interpolation here if interpolate == true
    // (animate movement over several frames)
    
    dirty = true;
    ESP_LOGI(TAG, "Window moved to (%d, %d)", x, y);
    //gotta clear the screen now, it's ass and leaves shit left on the screen all fucking over
    fb_clear(0x0000);
}

void Window::set_layer(uint8_t layer) {
    Initialcfg.Layer = layer;
    // Also update Currentcfg if you track it
    dirty = true;
    ESP_LOGI(TAG, "Window layer changed to %d", layer);
}

void Window::set_size(uint16_t width, uint16_t height) {
    if (wi_sizing.Width == width && wi_sizing.Height == height) return;
    
    wi_sizing.Width = width;
    wi_sizing.Height = height;
    
    calculateLogicalDimensions();
    dirty = true;
    ESP_LOGI(TAG, "Window resized to %dx%d", width, height);
}







static inline void rotPointLocal(
    int x, int y,
    int W, int H,
    int rot,
    int& xr, int& yr)
{
    switch (rot & 3) {
        case 0: xr = x;       yr = y;       break;
        case 1: xr = H-1-y;   yr = x;       break;
        case 2: xr = W-1-x;   yr = H-1-y;   break;
        case 3: xr = y;       yr = W-1-x;   break;
    }
}

void Window::LocalToScreen(int lx, int ly, int& sx, int& sy)
{
    int rx, ry;
    rotPointLocal(lx, ly, wi_sizing.Width, wi_sizing.Height, wi_sizing.rotation, rx, ry);
    sx = currentPhysX + rx;
    sy = currentPhysY + ry;
}

void Window::get_physical_bounds(int& out_x, int& out_y, int& out_w, int& out_h) {
    const int rot = wi_sizing.rotation & 3;
    const int rawW = wi_sizing.Width;
    const int rawH = wi_sizing.Height;
    
    // Rotated dimensions
    out_w = (rot % 2 == 0) ? rawW : rawH;
    out_h = (rot % 2 == 0) ? rawH : rawW;
    
    // Rotated position offset
    int offsetX, offsetY;
    rotPointLocal(0, 0, rawW, rawH, rot, offsetX, offsetY);
    out_x = wi_sizing.Xpos - offsetX;
    out_y = wi_sizing.Ypos - offsetY;
    
    // Clamp to screen (same as WinDraw)
    out_x = std::max(0, std::min(out_x, v_env.clamped_screen_dim_w - out_w));
    out_y = std::max(0, std::min(out_y, v_env.clamped_screen_dim_h - out_h));
}
// Helper function (add to MWenv.cpp or a utils header)
[[maybe_unused]] static int16_t parse_int(const stdpsram::String& str, int base) {
    int16_t result = 0;
    bool negative = false;
    size_t i = 0;

    if (str.empty()) return 0;

    if (str[0] == '-') {
        negative = true;
        ++i;
    }

    while (i < str.size()) {
        char c = str[i++];
        int digit;
        if (c >= '0' && c <= '9')      digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else break;

        result = result * base + digit;
    }

    return negative ? -result : result;
}

// Then in tokenize():
/*
//christ i have nothing but contempt for this part of code. how the fuck are these stupid fucking words and gybberish i'm typing making up a text tokenizer 
//how the fuck does linux do this shit
int val = parse_int(inside.substr(5));  // for size=
int16_t x = parse_int(inside.substr(4, comma - 4));
int16_t y = parse_int(inside.substr(comma + 1));
*/
// Helper: parse integer from stdpsram::String substring (no exceptions, fallback to default)


// Anonymous namespace = visible only in this .cpp file


// ====================== FAST UI8 TO 2 CHARS ======================
inline void ui8Tostr(uint8_t v, std::string& out, size_t pos = std::string::npos) {
    static const char digits[] = "0123456789";
    uint8_t tens = (static_cast<uint16_t>(v) * 205U) >> 11;
    uint8_t ones = v - tens * 10U;

    if (pos == std::string::npos) {
        out.push_back(digits[tens]);
        out.push_back(digits[ones]);
    } else {
        if (pos + 2 > out.size()) out.resize(pos + 2);
        out[pos] = digits[tens];
        out[pos + 1] = digits[ones];
    }
}



// ====================== FASTER PARSERS ======================

int safe_parse_int(std::string_view str, int default_val) {
    if (str.empty()) return default_val;

    int sign = 1;
    size_t i = 0;

    if (str[0] == '-') { sign = -1; ++i; }
    else if (str[0] == '+') { ++i; }

    int result = 0;
    bool digits_found = false;

    while (i < str.size()) {
        char c = str[i];
        if (c < '0' || c > '9') break;
        result = result * 10 + (c - '0');
        digits_found = true;
        ++i;
    }

    return digits_found ? result * sign : default_val;
}

uint16_t safe_parse_color(std::string_view str, uint16_t default_val) {
    if (str.empty()) return default_val;

    size_t start = 0;
    int base = 10;

    if (str.size() >= 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        base = 16;
        start = 2;
    }

    uint32_t result = 0;
    bool digits_found = false;

    for (size_t i = start; i < str.size(); ++i) {
        char c = str[i];
        int digit = -1;

        if (c >= '0' && c <= '9')      digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;

        if (digit == -1) break;

        result = result * base + digit;
        digits_found = true;

        if (result > 0xFFFF) return default_val;
    }

    return digits_found ? static_cast<uint16_t>(result) : default_val;
}

// ====================== IMPROVED TOKENIZER ======================

stdpsram::Vector<TextChunk> Window::tokenize(const stdpsram::String& input)
{
    stdpsram::Vector<TextChunk> chunks;
    if (input.empty()) return chunks;

    const char* data = input.c_str();
    const size_t len = input.length();
    chunks.reserve(std::max<size_t>(8, len / 16));

    size_t i = 0;
    size_t run_start = 0;

    auto flush_run = [&](size_t end) {
        if (end > run_start) {
            PlainTextRef ref{
                static_cast<uint32_t>(run_start),
                static_cast<uint32_t>(end - run_start)
            };
            chunks.emplace_back(ref);
        }
    };

    while (i < len) {
        if (!(data[i] == '<' && (i + 1) < len && data[i + 1] == '|')) {
            ++i;
            continue;
        }

        flush_run(i);

        size_t end = i + 2;
        while (end + 1 < len && !(data[end] == '|' && data[end + 1] == '>'))
            ++end;

        if (end + 1 >= len) {
            run_start = i;   // unclosed tag → rest is plain
            break;
        }

        const size_t inside_off = i + 2;
        const size_t inside_len = end - inside_off;
        i = end + 2;
        run_start = i;

        if (inside_len == 0) continue;

        std::string_view inside(data + inside_off, inside_len);

        if (inside_len <= 2) {
            bool is_off = (inside_len == 2 && inside[0] == '/');
            char first  = is_off ? inside[1] : inside[0];
            switch (first) {
                case 'n': if (!is_off) chunks.emplace_back(TagType::LineBreak); break;
                case 'u': chunks.emplace_back(is_off ? TagType::UnderlineOff : TagType::UnderlineToggle); break;
                case 's': chunks.emplace_back(is_off ? TagType::StrikethroughOff : TagType::StrikethroughToggle); break;
                case 'b': chunks.emplace_back(is_off ? TagType::BoldOff : TagType::BoldToggle); break;
                case 'i': chunks.emplace_back(is_off ? TagType::ItalicOff : TagType::ItalicToggle); break;
                default: break;
            }
            continue;
        }

        if (inside.starts_with("size=")) {
            int sz = safe_parse_int(inside.substr(5), 1);
            if (sz >= 1 && sz <= 16)
                chunks.emplace_back(TagType::SizeChange, SizeTag{static_cast<uint8_t>(sz)});
        }
        else if (inside.starts_with("color=")) {
            chunks.emplace_back(TagType::ColorChange,
                ColorTag{safe_parse_color(inside.substr(6))});
        }
        else if (inside.starts_with("hl=")) {
            chunks.emplace_back(TagType::HighlightChange,
                HighlighterTag{safe_parse_color(inside.substr(3), 0xFFFF), true});
        }
        else if (inside.starts_with("pos=")) {
            size_t comma = inside.find(',', 4);
            if (comma != std::string_view::npos) {
                chunks.emplace_back(TagType::PosChange, PosTag{
                    (int16_t)safe_parse_int(inside.substr(4, comma - 4)),
                    (int16_t)safe_parse_int(inside.substr(comma + 1))
                });
            }
        }
    }

    flush_run(len);
    return chunks;
}




// ──────────────────────────────────────────────
// WinDraw() – logical origin fixed at (Posx, Posy) for ALL rotations
// ──────────────────────────────────────────────


//todo: draw a rect for the -[]x thing windows have at the top, plus their name in the middle
//i guess i can do this via using a miniturized version of the blit function, using three bitmaps with seperate colrors i think

//differ to generate tile if it is not valid, then if it is valid, draw on screen
//is it normal? 
//looks like it's valid, tile that motherfucker into the right positions in psram
//get the size of this window, and it's rotation, then tile by puking it into the sram.
// we'll need to cut it off, because they're 32*32 tiles, which are 2d, but the arrays are 1d
//so we'll just get the buffer width(relative because the coord space) and see the "width/height remaining" so if we hit the end, we say "oh, skip (32-remaining) pixels in our pseudoframebuffer tile"
//then we return to the NEXT line and do it again
//i should add a check to make sure we don't regen the pattern every time we draw, i'lljust check if it's changed. eg LastDraw=!pattern_this_draw
//it does not exist, presumably because it didn't exist when this window started existing
//create a new one and bind it to our psram reference
//create the background tile and set our object pointer to it
//whatever the fresh hell it may be
//okay now try again to push it
    
void Window::HaltDrawing() {
    drawing_halted = true;
    dirty = false;
    ESP_LOGW(TAG, "Window '%s' drawing halted", Currentcfg.name);
}

void Window::ResumeDrawing() {
    drawing_halted = false;
    dirty = true;
}

// Text bounds fix for prior recurring issue
//
// Problem:
//   Layout assumes ink is [cursor .. cursor+width] x [cursor .. cursor+height]
//   in *logical* +X/+Y.  fb_draw_text with angle grows ink along (ax,ay)/(ux,uy).
//   At size>=2 that box sticks out of the window (often "above" after rot=1).
//
// Fix:
//   1. Know the screen-space AABB of a run for each angle
//   2. Shift (sx,sy) so the full AABB stays inside the window physical rect
//   3. Keep logical clamp so soft-wrap / line advance still work
// =============================================================================

// ---------------------------------------------------------------------------
// Helper: screen AABB of a text run for a given fb_draw_text angle
//   origin (ox,oy) = the (x,y) you pass to fb_draw_text
//   run_w  = strlen * glyph_w * size     (advance direction extent)
//   run_h  = glyph_h * size              (glyph-row direction extent)
// ---------------------------------------------------------------------------
static inline void text_run_aabb(
    uint8_t angle, int ox, int oy, int run_w, int run_h,
    int& out_x0, int& out_y0, int& out_x1, int& out_y1)
{
    switch (angle & 0x1F) {
        case 4:  // 90°  advance +Y, glyph rows -X
            out_x0 = ox - run_h;
            out_y0 = oy;
            out_x1 = ox;
            out_y1 = oy + run_w;
            break;
        case 8:  // 180° advance -X, glyph rows -Y
            out_x0 = ox - run_w;
            out_y0 = oy - run_h;
            out_x1 = ox;
            out_y1 = oy;
            break;
        case 12: // 270° advance -Y, glyph rows +X
            out_x0 = ox;
            out_y0 = oy - run_w;
            out_x1 = ox + run_h;
            out_y1 = oy;
            break;
        default: // 0°   advance +X, glyph rows +Y
            out_x0 = ox;
            out_y0 = oy;
            out_x1 = ox + run_w;
            out_y1 = oy + run_h;
            break;
    }
}

// Shift origin so the run AABB sits inside [winX, winX+winW) x [winY, winY+winH)
static inline void fit_text_origin_in_window(
    uint8_t angle, int& ox, int& oy,
    int run_w, int run_h,
    int winX, int winY, int winW, int winH)
{
    int x0, y0, x1, y1;
    text_run_aabb(angle, ox, oy, run_w, run_h, x0, y0, x1, y1);

    if (x0 < winX)           ox += (winX - x0);
    if (y0 < winY)           oy += (winY - y0);
    if (x1 > winX + winW)    ox -= (x1 - (winX + winW));
    if (y1 > winY + winH)    oy -= (y1 - (winY + winH));
}

void Window::WinDraw() {
	
	if (drawing_halted || !IsWindowShown) return;

    
    
    
    //  ensure any position changes actually fuckin propogate
static uint16_t last_x = 0xFFFF;
static uint16_t last_y = 0xFFFF;

if (last_x != wi_sizing.Xpos || last_y != wi_sizing.Ypos) {
    ESP_LOGI(TAG, "Window '%s' position changed from (%d,%d) to (%d,%d)", 
             Currentcfg.name, last_x, last_y, wi_sizing.Xpos, wi_sizing.Ypos);
    last_x = wi_sizing.Xpos;
    last_y = wi_sizing.Ypos;
    //fb_clear(0x0000); //refresh after pos change
}

       
    
    
    if (!window_highlighted && (!dirty && !enable_refresh_override)) return;
     //changed, highlighted window will NOT allow early ret
    
	
    calculateLogicalDimensions();

    const int rot   = wi_sizing.rotation & 3;
    const int rawW  = wi_sizing.Width;
    const int rawH  = wi_sizing.Height;

    const int physW = logicalW;
    const int physH = logicalH;

    // === Window position with rotation offset ===
    int offsetX, offsetY;
    rotPointLocal(0, 0, rawW, rawH, rot, offsetX, offsetY);
    int physX = wi_sizing.Xpos - offsetX;
    int physY = wi_sizing.Ypos - offsetY;

    // Clamp to screen
    physX = std::max(0, std::min(physX, v_env.clamped_screen_dim_w - physW));
    physY = std::max(0, std::min(physY, v_env.clamped_screen_dim_h - physH));

  //  ESP_LOGI(TAG, "WinDraw rot=%d | logical(%dx%d) @ (%d,%d) → phys(%d,%d %dx%d)",rot, rawW, rawH, wi_sizing.Xpos, wi_sizing.Ypos, physX, physY, physW, physH);

    // === 1. BACKGROUND ===
    uint16_t clipX = physX;
    uint16_t clipY = physY;
    uint16_t clipW = physW;
    uint16_t clipH = physH;

    if (win_backgroundpattern == BgFillType::Solid) {
        fb_rect(true, 1, physX, physY, physW, physH,
                win_internal_color_background, win_internal_color_border);
    } else {
        if (!bgTile || bgTile->pbt_cfg.fill_type != win_backgroundpattern ||
            bgTile->primaryColor != bgPrimaryColor ||
            bgTile->secondaryColor != bgSecondaryColor) {
            setupBackgroundTile();
        }

        if (bgTile && bgTile->allocated) {
            const uint16_t TW = bgTile->pbt_cfg.tileSize_x;
            const uint16_t TH = bgTile->pbt_cfg.tileSize_y;

            for (uint16_t ly = 0; ly < rawH; ly += TH) {
                for (uint16_t lx = 0; lx < rawW; lx += TW) {
                    int sx, sy;
                    rotPointLocal(lx, ly, rawW, rawH, rot, sx, sy);
                    sx += physX;
                    sy += physY;
                    blit_tile_clipped(static_cast<uint16_t>(sx),
                                      static_cast<uint16_t>(sy),
                                      clipX, clipY, clipW, clipH,
                                      framebuffer,
                                      bgTile->pseudoframebuffer,
                                      TW, TH);
                }
            }
        } else {
            // Fallback to solid colour if tile failed
            fb_rect(true, 1, physX, physY, physW, physH,
                    win_internal_color_background, win_internal_color_border);
        }
    }

    // === 2. TOP BAR (if enabled) ===
    if (win_internal_optionsBitmask & WIN_OPT_SHOW_TOP_BAR_MENU ||
        Currentcfg.ShowNameAtTopOfWindow) {
        const int bar_height = 24;
        fb_rect(true, 1, physX, physY, physW, bar_height,
                win_internal_color_border, win_internal_color_border);
        fb_draw_text(physX + 6, physY + 4, physW - 40,
                     Currentcfg.name,
                     0xFFFF, 1, 0, true, 0x0000, 40,
                     w_font_info);
    }

    // === 3. NORMAL OUTER BORDER (only if not borderless) ===
    if (!Currentcfg.borderless) {
        fb_rect(false, 1, physX, physY, physW, physH, 0x0000, win_internal_color_border);
    }

    
      // === 4. TEXT & CONTENT ===
    if (content_dirty || !isTokenized) {
        if (text_mtx) xSemaphoreTake(text_mtx, portMAX_DELAY);
        cachedChunks.clear();
        {
            auto fresh = tokenize(content);
            cachedChunks.swap(fresh);
        }
        isTokenized   = true;
        content_dirty = false;
        if (text_mtx) xSemaphoreGive(text_mtx);
    }

    Tstate.color = win_internal_color_text;
    Tstate.size  = Currentcfg.TextSizeMult;
    if (Tstate.size < 1) Tstate.size = 1;
    Tstate.underline = Tstate.strikethrough = Tstate.bold = Tstate.italic = false;
    Tstate.highlight_bg = 0;

    const int text_rot_flag = (rot & 3) * 4;   // 0,4,8,12
    const int glyph_w = w_font_info.fcs.x;     // 6
    const int glyph_h = w_font_info.fcs.y;     // 8
    const int line_gap = 2;

    // Physical window rect (already rotation-aware via get_physical_bounds /
    // the physX/physY + logicalW/H you compute at the top of WinDraw)
    const int winX = (int)currentPhysX;
    const int winY = (int)currentPhysY;
    const int winW = (rot % 2 == 0) ? rawW : rawH;
    const int winH = (rot % 2 == 0) ? rawH : rawW;

    // Logical padding – at least 2, and never smaller than half a glyph at
    // current size so the first line cannot start with ink outside the box.
    const int pad = 2;
    const int max_local_x = rawW - pad;
    const int max_local_y = rawH - pad;

    int curLX = pad;
    int curLY = pad;
    int line_max_size = Tstate.size;

    std::string draw_buf;
    draw_buf.reserve(64);

    for (const auto& chunk : cachedChunks) {
        switch (chunk.kind) {

        case TagType::PlainText: {
            const PlainTextRef* ref = std::get_if<PlainTextRef>(&chunk.content);
            if (!ref || ref->length == 0) break;
            if ((size_t)ref->offset + ref->length > content.size()) break;

            if (Tstate.size > line_max_size)
                line_max_size = Tstate.size;

            const int run_px = (int)ref->length * glyph_w * Tstate.size; // advance dir
            const int run_py = glyph_h * Tstate.size;                    // glyph-row dir

            // Soft-wrap in logical space
            if (curLX > pad && curLX + run_px > max_local_x) {
                curLX = pad;
                curLY += glyph_h * line_max_size + line_gap;
                line_max_size = Tstate.size;
            }

            // Past bottom of logical window – stop
            if (curLY + run_py > max_local_y && curLY > pad)
                break;

            int drawLX = (curLX < pad) ? pad : curLX;
            int drawLY = (curLY < pad) ? pad : curLY;

            int rx, ry;
            rotPointLocal(drawLX, drawLY, rawW, rawH, rot, rx, ry);
            int sx = physX + rx;
            int sy = physY + ry;

            // *** KEY FIX: pull origin so full ink stays inside window ***
            fit_text_origin_in_window(
                (uint8_t)text_rot_flag, sx, sy,
                run_px, run_py,
                winX, winY, winW, winH
            );

            draw_buf.assign(content.c_str() + ref->offset, ref->length);

            fb_draw_text(
                (uint8_t)text_rot_flag,
                sx, sy,
                draw_buf.c_str(),
                Tstate.color,
                (uint8_t)Tstate.size,
                0,
                (Tstate.highlight_bg != 0),
                Tstate.highlight_bg ? Tstate.highlight_bg : win_internal_color_background,
                0,
                w_font_info
            );

            curLX += run_px;
            if (curLX > max_local_x) {
                curLX = pad;
                curLY += glyph_h * line_max_size + line_gap;
                line_max_size = Tstate.size;
            }
            break;
        }

        case TagType::LineBreak:
            curLX = pad;
            curLY += glyph_h * line_max_size + line_gap;
            line_max_size = Tstate.size;
            break;

        case TagType::PosChange:
            if (auto* p = std::get_if<PosTag>(&chunk.content)) {
                curLX = std::max(pad, std::min((int)p->x, max_local_x));
                curLY = std::max(pad, std::min((int)p->y, max_local_y));
                line_max_size = Tstate.size;
            }
            break;

        case TagType::SizeChange:
            if (auto* p = std::get_if<SizeTag>(&chunk.content)) {
                int s = p->value;
                if (s >= 1 && s <= 16) {
                    Tstate.size = s;
                    if (s > line_max_size) line_max_size = s;
                }
            }
            break;

        case TagType::ColorChange:
            if (auto* p = std::get_if<ColorTag>(&chunk.content))
                Tstate.color = p->value;
            break;

        case TagType::HighlightChange:
            if (auto* p = std::get_if<HighlighterTag>(&chunk.content))
                Tstate.highlight_bg = p->enabled ? p->color : 0;
            break;

        case TagType::UnderlineToggle:     Tstate.underline = true;  break;
        case TagType::UnderlineOff:        Tstate.underline = false; break;
        case TagType::StrikethroughToggle: Tstate.strikethrough = true;  break;
        case TagType::StrikethroughOff:    Tstate.strikethrough = false; break;
        case TagType::BoldToggle:          Tstate.bold = true;  break;
        case TagType::BoldOff:             Tstate.bold = false; break;
        case TagType::ItalicToggle:        Tstate.italic = true;  break;
        case TagType::ItalicOff:           Tstate.italic = false; break;
        default: break;
        }
    }

    // === 5. HIGHLIGHT DASHED BORDER (ONLY if window is highlighted) ===
    if (window_highlighted) {
        // Only update the border color every 10 frames
        static uint8_t frame_counter = 0;
        frame_counter++;
        
        uint16_t border_color;
        if (frame_counter >= 10) {
            // Every 10th frame, toggle the blink state
            TenthTick = !TenthTick;
            frame_counter = 0;
        }
        
        // Use TenthTick for blinking (now updates 10x slower)
        border_color = (TenthTick & 1) ? 0x0000 : 0xFFFF;
        
        fb_rect_border(false, 2, physX, physY, physW, physH,
                       Currentcfg.BorderColor,
                       border_color,
                       8);  // segment_len (dash length)
        
     //   ESP_LOGI(TAG, "highlighted this window! TenthTick=%d frame=%d", TenthTick, frame_counter);
    } else {
        // no highlight today
    }
    

    //============================6. warning experemental feature===========canvas===========
DrawCanvas();



    // === FINISH ===
    currentPhysX = physX;
    currentPhysY = physY;
    // NO TenthTick toggle here anymore — it's handled above every 10 frames
   // ESP_LOGI(TAG, "highlight=%d TenthTick=%d dirty=%d",  window_highlighted, TenthTick, dirty);
    dirty = false;
    lastUpdateTime = esp_timer_get_time();
}

//guess who found out she needed to do this a lot after making the window system and working on other drivers
//i swear to god bruh
void Window::SetText(const stdpsram::String& newText)
{
    if (text_mtx) xSemaphoreTake(text_mtx, portMAX_DELAY);
    content = newText;
    isTokenized   = false;
    content_dirty = true;
    dirty         = true;
    if (text_mtx) xSemaphoreGive(text_mtx);
}

void Window::SetText(const char* newText)
{
    if (text_mtx) xSemaphoreTake(text_mtx, portMAX_DELAY);
    if (!newText) {
        content.clear();
        cachedChunks.clear();
    } else {
        content = stdpsram::String(newText);
    }
    isTokenized   = false;
    content_dirty = true;
    dirty         = true;
    if (text_mtx) xSemaphoreGive(text_mtx);
}

void Window::ClearText()
{
    if (text_mtx) xSemaphoreTake(text_mtx, portMAX_DELAY);
    cachedChunks.clear();
    content.clear();
    last_content.clear();
    isTokenized   = false;
    content_dirty = true;
    dirty         = true;
    if (text_mtx) xSemaphoreGive(text_mtx);
}

void Window::SetText(std::string_view text) {
    content = stdpsram::String(text.data(), text.size());
    isTokenized = false;
    content_dirty = true;
    dirty = true;
}

void Window::SetText(const std::string& newText) {
    content = stdpsram::String(newText.begin(), newText.end());
    isTokenized = false;
    content_dirty = true;
    dirty = true;
}



void Window::AppendText(const stdpsram::String& moreText) {
    content.append(moreText);
    isTokenized = false;
    content_dirty = true;
    dirty = true;
}

void Window::AppendText(const std::string& moreText) {
    content.append(stdpsram::String(moreText.begin(), moreText.end()));
    isTokenized = false;
    content_dirty = true;
    dirty = true;
}
/*
void Window::ClearText() {
    // Destroy chunks first while allocator is still healthy, then strings
    cachedChunks.clear();
    cachedChunks.shrink_to_fit();   // release PSRAM capacity held by the vector
    content.clear();
    last_content.clear();
    isTokenized = false;
    content_dirty = true;
    dirty = true;
}*/

//remember, dumbass,
/*
SetText()
    ↓
content changes
    ↓
isTokenized = false
    ↓
WinDraw()
    ↓
tokenize(content)   (only once)
    ↓
cachedChunks
    ↓
render cached chunks every frame
*/


// WindowManager constructor - initialize members
WindowManager::WindowManager() 
    : m_toolbarConfig(g_defaultToolbarConfig)
    , tb_dirty(true)
    , last_toolbar_update(0)
{
}

WindowManager::~WindowManager() {

}

// Add after your existing WindowManager functions in MWenv.cpp

bool WindowManager::registerWindow(std::shared_ptr<Window> window) {
    if (!window) {
        ESP_LOGE(TAG, "registerWindow: window is null!");
        return false;
    }
    
    // Bounds check - adjust window position if it overlaps toolbar
    if (m_toolbarConfig.showToolbar) {
        uint16_t offset = GetToolbarOffset();
        
        switch(m_toolbarConfig.tb_rot) {
            case 0: // Top
                if (window->wi_sizing.Ypos < offset) {
                    ESP_LOGW(TAG, "Window overlapped top toolbar, moving from Y=%d to %d", 
                             window->wi_sizing.Ypos, offset);
                    window->wi_sizing.Ypos = offset;
                    window->dirty = true;
                }
                // Also clamp height
                if (window->wi_sizing.Ypos + window->wi_sizing.Height > v_env.clamped_screen_dim_h) {
                    window->wi_sizing.Height = v_env.clamped_screen_dim_h - window->wi_sizing.Ypos;
                }
                break;
                
            case 1: // Left
                if (window->wi_sizing.Xpos < offset) {
                    ESP_LOGW(TAG, "Window overlapped left toolbar, moving from X=%d to %d", 
                             window->wi_sizing.Xpos, offset);
                    window->wi_sizing.Xpos = offset;
                    window->dirty = true;
                }
                break;
                
            case 2: // Bottom
                if (window->wi_sizing.Ypos + window->wi_sizing.Height > v_env.clamped_screen_dim_h - offset) {
                    int new_y = v_env.clamped_screen_dim_h - offset - window->wi_sizing.Height;
                    if (new_y < 0) new_y = 0;
                    ESP_LOGW(TAG, "Window overlapped bottom toolbar, moving from Y=%d to %d", 
                             window->wi_sizing.Ypos, new_y);
                    window->wi_sizing.Ypos = new_y;
                    window->dirty = true;
                }
                break;
                
            case 3: // Right
                if (window->wi_sizing.Xpos + window->wi_sizing.Width > v_env.clamped_screen_dim_w - offset) {
                    int new_x = v_env.clamped_screen_dim_w - offset - window->wi_sizing.Width;
                    if (new_x < 0) new_x = 0;
                    ESP_LOGW(TAG, "Window overlapped right toolbar, moving from X=%d to %d", 
                             window->wi_sizing.Xpos, new_x);
                    window->wi_sizing.Xpos = new_x;
                    window->dirty = true;
                }
                break;
        }
    }
    
    windows.push_back(window);
   // ESP_LOGI(TAG, "Window registered at pos(%d,%d) size(%dx%d), total: %d", window->wi_sizing.Xpos, window->wi_sizing.Ypos, window->wi_sizing.Width, window->wi_sizing.Height,(int)windows.size());
    return true;
}

bool WindowManager::unregisterWindow(std::shared_ptr<Window> window) {
    if (!window) {
        ESP_LOGE(TAG, "unregisterWindow: window is null!");
        return false;
    }
    
    // Find the window in the vector
    auto it = std::find(windows.begin(), windows.end(), window);
    if (it == windows.end()) {
        ESP_LOGW(TAG, "unregisterWindow: window '%s' not found in registry", 
                 window->Currentcfg.name);
        return false;
    }
    
    // Mark window as hidden before removal
    window->IsWindowShown = false;
    window->dirty = true;
    
    // Remove from vector
    windows.erase(it);
    
    ESP_LOGI(TAG, "Window '%s' unregistered, remaining windows: %d", 
             window->Currentcfg.name, (int)windows.size());
    
    // Force a full screen redraw since a window was removed
    fb_clear(0x0000);  // Clear screen to background color
    
    return true;
}

bool WindowManager::PruneDeadWindows() {
    size_t before = windows.size();
    windows.erase(std::remove_if(windows.begin(), windows.end(),
        [](const auto& w) {
            return !w || !w->IsWindowShown;
        }), windows.end());
    
    size_t after = windows.size();
    if (before != after) {
        ESP_LOGI(TAG, "Pruned %d dead windows", (int)(before - after));
    }
    return before != after;
}

void WindowManager::ClampWinToArea(s_bounds_16u bounds, std::shared_ptr<Window> target) {
    if (!target) return;

    // Simple clamping
    if (target->wi_sizing.Xpos < bounds.x) target->wi_sizing.Xpos = bounds.x;
    if (target->wi_sizing.Ypos < bounds.y) target->wi_sizing.Ypos = bounds.y;
    if (target->wi_sizing.Xpos + target->wi_sizing.Width > bounds.x + bounds.w) {
        target->wi_sizing.Width = (bounds.x + bounds.w) - target->wi_sizing.Xpos;
    }
    if (target->wi_sizing.Ypos + target->wi_sizing.Height > bounds.y + bounds.h) {
        target->wi_sizing.Height = (bounds.y + bounds.h) - target->wi_sizing.Ypos;
    }

    target->dirty = true;
}


void WindowManager::ClampToArea(s_bounds_16u bounds, std::shared_ptr<Window> window, bool is_universal) {
    if (is_universal) {
        for (auto& win : windows) {
            if (!win) continue;
            ClampWinToArea(bounds, win);
        }
    }else{
        //just this one
        ClampWinToArea(bounds, window); 
//you were in the middle of fixing the clamp to area bug to avoid oscilation and repositioning of the window

    }
}



// Add these helper functions at the top (after includes)
static void draw_toolbar_background(int x, int y, int width, int height, uint16_t color) {
    fb_rect(true, 1, x, y, width, height, color, color);
}

static void draw_toolbar_text(int x, int y, const char* text, uint16_t text_color) {
    if (!text || !text[0]) return;
    fb_draw_text(x, y, 200, text, text_color, 1, 0, true, 0x0000, 40, ft_AVR_classic_6x8);
}

// NEW: Implementation in WindowManager class
uint16_t WindowManager::GetAvailableWidth() {
    if (!m_toolbarConfig.showToolbar) return v_env.clamped_screen_dim_w;
    
    // For left/right toolbars, subtract width
    if (m_toolbarConfig.tb_rot == 1 || m_toolbarConfig.tb_rot == 3) {
        return v_env.clamped_screen_dim_w - 32;  // toolbar takes 32px on sides
    }
    return v_env.clamped_screen_dim_w;
}

uint16_t WindowManager::GetAvailableHeight() {
    if (!m_toolbarConfig.showToolbar) return v_env.clamped_screen_dim_h;
    
    // For top/bottom toolbars, subtract height
    if (m_toolbarConfig.tb_rot == 0 || m_toolbarConfig.tb_rot == 2) {
        return v_env.clamped_screen_dim_h - 24;  // toolbar takes 24px on top/bottom
    }
    return v_env.clamped_screen_dim_h;
}

uint16_t WindowManager::GetToolbarOffset() {
    if (!m_toolbarConfig.showToolbar) return 0;
    
    switch(m_toolbarConfig.tb_rot) {
        case 0: return 24;  // top: windows start 24px down
        case 1: return 32;  // left: windows start 32px right
        case 2: return 0;   // bottom: windows start at top
        case 3: return 0;   // right: windows start at left
        default: return 0;
    }
}


void WindowManager::SetToolbarActive(bool on) {
    m_toolbarConfig.showToolbar = on;
    tb_dirty = true;
    windows_repositioned = false;  // Need to reposition windows again
    for (auto& win : windows) {
        if (win) win->dirty = true;
    }
}

void WindowManager::setToolbarRot(uint8_t new_rot) {
    if (new_rot > 3) new_rot = 0;
    m_toolbarConfig.tb_rot = new_rot;
    tb_dirty = true;
    windows_repositioned = false;  // Need to reposition windows again
}

void WindowManager::addToolbarIco(s_bmp_t& icon) {
    // Find first empty slot
    for (int i = 0; i < 16; i++) {
        if (!m_toolbarConfig.ref_iconptrs[i]) {
            m_toolbarConfig.ref_iconptrs[i] = &icon;
            m_toolbarConfig.icons_shown = (toolbar_items_t)(m_toolbarConfig.icons_shown | (1 << i));
            tb_dirty = true;
            break;
        }
    }
}

void WindowManager::SetToolbarText(const char* text) {
    if (text) {
        toolbar_text = text;
    } else {
        toolbar_text.clear();
    }
    tb_dirty = true;
}



void WindowManager::DrawToolBar() {
    if (!m_toolbarConfig.showToolbar) return;
    
    static uint16_t last_color = 0xFFFF;
    static bool last_visibility = false;
    static uint8_t last_rotation = 0xFF;
    
    // Only redraw if something changed
    if (!tb_dirty && 
        last_color == m_toolbarConfig.color && 
        last_visibility == m_toolbarConfig.showToolbar &&
        last_rotation == m_toolbarConfig.tb_rot) {
        return;  // Skip redraw if nothing changed
    }
    
    last_color = m_toolbarConfig.color;
    last_visibility = m_toolbarConfig.showToolbar;
    last_rotation = m_toolbarConfig.tb_rot;
    
    int bar_width = v_env.clamped_screen_dim_w;
    int bar_height = v_env.clamped_screen_dim_h;
    int bar_x = 0;
    int bar_y = 0;
    int bar_thickness = 28;
    
    switch(m_toolbarConfig.tb_rot) {
        case 0:  // top
            bar_x = 0;
            bar_y = 0;
            bar_width = v_env.clamped_screen_dim_w;
            bar_height = bar_thickness;
            break;
        case 1:  // left
            bar_x = 0;
            bar_y = 0;
            bar_width = bar_thickness;
            bar_height = v_env.clamped_screen_dim_h;
            break;
        case 2:  // bottom
            bar_x = 0;
            bar_y = v_env.clamped_screen_dim_h - bar_thickness;
            bar_width = v_env.clamped_screen_dim_w;
            bar_height = bar_thickness;
            break;
        case 3:  // right
            bar_x = v_env.clamped_screen_dim_w - bar_thickness;
            bar_y = 0;
            bar_width = bar_thickness;
            bar_height = v_env.clamped_screen_dim_h;
            break;
        default:
            return;
    }
    
    // Bounds check
    if (bar_x < 0) bar_x = 0;
    if (bar_y < 0) bar_y = 0;
    if (bar_x + bar_width > v_env.clamped_screen_dim_w) 
        bar_width = v_env.clamped_screen_dim_w - bar_x;
    if (bar_y + bar_height > v_env.clamped_screen_dim_h) 
        bar_height = v_env.clamped_screen_dim_h - bar_y;
    
   
    fb_rect(1,2,bar_x, bar_y, bar_width, bar_height, m_toolbarConfig.color,0xFFFF);
    
    // Draw time/date text (top/bottom only)
    if ((m_toolbarConfig.tb_rot == 0 || m_toolbarConfig.tb_rot == 2) && !toolbar_text.empty()) {
        int text_x = bar_x + (bar_width / 2) - (strlen(toolbar_text.c_str()) * 3);
        int text_y = bar_y + 6;
        
        // ✅ OPTIMIZATION 2: Clip text if too long
        if (text_x < bar_x) text_x = bar_x + 2;
        if (text_x + strlen(toolbar_text.c_str()) * 6 > bar_x + bar_width) {
            // Text too long, truncate or skip
            static char truncated[32];
            strncpy(truncated, toolbar_text.c_str(), sizeof(truncated) - 4);
            strcat(truncated, "...");
            fb_draw_text(text_x, text_y, bar_width - 4, truncated, 0xFFFF, 1, 0, true, 0x0000, 40, ft_AVR_classic_6x8);
        } else {
            fb_draw_text(text_x, text_y, bar_width - 4, toolbar_text.c_str(), 0xFFFF, 1, 0, true, 0x0000, 40, ft_AVR_classic_6x8);
        }
    }
    
    // Draw icons
    int icon_x = bar_x + 4;
    int icon_y = bar_y + 4;
    
    for (int i = 0; i < 16; i++) {
        if (m_toolbarConfig.ref_iconptrs[i] && (m_toolbarConfig.icons_shown & (1 << i))) {
            if (m_toolbarConfig.tb_rot == 0 || m_toolbarConfig.tb_rot == 2) {
                if (icon_x + 16 <= bar_x + bar_width - 4) {
                    fb_draw_bitmap(icon_x, icon_y, 16, 16, m_toolbarConfig.ref_iconptrs[i]->data);
                    icon_x += 20;
                }
            } else {
                if (icon_y + 16 <= bar_y + bar_height - 4) {
                    fb_draw_bitmap(icon_x, icon_y, 16, 16, m_toolbarConfig.ref_iconptrs[i]->data);
                    icon_y += 20;
                }
            }
        }
    }
    
    tb_dirty = false;
}



void WindowManager::RepositionAllWindows() {
    if (!m_toolbarConfig.showToolbar) return;
    if (fs_state.fullscreen_win) return;
    if (windows_repositioned) return;  // Only reposition once
    
    uint16_t offset = GetToolbarOffset();
    bool moved = false;
    
    for (auto& win : windows) {
        if (!win) continue;
        
        int physX, physY, physW, physH;
        win->get_physical_bounds(physX, physY, physW, physH);
        
        switch (m_toolbarConfig.tb_rot) {
            case 0: { // top toolbar
                if (physY < offset) {
                    int delta = offset - physY;
                    // Check if moving would exceed the bottom of screen
                    int newY = win->wi_sizing.Ypos + delta;
                    int maxY = v_env.clamped_screen_dim_h - physH;
                    if (newY > maxY) {
                        newY = maxY;
                    }
                    win->wi_sizing.Ypos = newY;
                    win->dirty = true;
                    moved = true;  // ← SET moved TO TRUE!
                    ESP_LOGI(TAG, "Window '%s' moved down by %d (physical Y %d → %d)", 
                             win->Currentcfg.name, delta, physY, physY + delta);
                }
                break;
            }
                
            case 1: { // left toolbar
                if (physX < offset) {
                    int delta = offset - physX;
                    // Check if moving would exceed the right edge of screen
                    int newX = win->wi_sizing.Xpos + delta;
                    int maxX = v_env.clamped_screen_dim_w - physW;
                    if (newX > maxX) {
                        newX = maxX;
                    }
                    win->wi_sizing.Xpos = newX;
                    win->dirty = true;
                    moved = true;  // ← SET moved TO TRUE!
                    ESP_LOGI(TAG, "Window '%s' moved right by %d (physical X %d → %d)", 
                             win->Currentcfg.name, delta, physX, physX + delta);
                }
                break;
            }
                
            case 2: { // bottom toolbar
                int max_phys_y = v_env.clamped_screen_dim_h - offset - physH;
                if (physY > max_phys_y) {
                    int delta = physY - max_phys_y;
                    // Check if moving would exceed the top of screen
                    int newY = win->wi_sizing.Ypos - delta;
                    if (newY < 0) {
                        newY = 0;
                    }
                    win->wi_sizing.Ypos = newY;
                    win->dirty = true;
                    moved = true;  // ← SET moved TO TRUE!
                    ESP_LOGI(TAG, "Window '%s' moved up by %d", 
                             win->Currentcfg.name, delta);
                }
                break;
            }
                
            case 3: { // right toolbar
                int max_phys_x = v_env.clamped_screen_dim_w - offset - physW;
                if (physX > max_phys_x) {
                    int delta = physX - max_phys_x;
                    // Check if moving would exceed the left edge of screen
                    int newX = win->wi_sizing.Xpos - delta;
                    if (newX < 0) {
                        newX = 0;
                    }
                    win->wi_sizing.Xpos = newX;
                    win->dirty = true;
                    moved = true;  // ← SET moved TO TRUE!
                    ESP_LOGI(TAG, "Window '%s' moved left by %d", 
                             win->Currentcfg.name, delta);
                }
                break;
            }
        }
    }
    
    // Moved this OUTSIDE the loop
    if (moved) {
        windows_repositioned = true;
        tb_dirty = true;
        ESP_LOGI(TAG, "Toolbar repositioning completed");
        // fb_clear(0x0000); //change to background color fixit
    }
}




// In cpp
void WindowManager::SortWindowsByZOrder() {
    std::sort(windows.begin(), windows.end(),
        [](const std::shared_ptr<Window>& a, const std::shared_ptr<Window>& b) {
            if (!a || !b) return a != nullptr;  // nulls go to end
            return a->wi_sizing.Zorder < b->wi_sizing.Zorder;  // lower Z = higher priority (draw later)
        });
}


// ====================== FULLSCREEN MANAGEMENT ======================

void WindowManager::make_window_fullscreen(std::shared_ptr<Window> win){
    if (!win) return;

    ESP_LOGI(TAG, "=== FULLSCREEN LOCK ENGAGED for '%s' ===", win->Currentcfg.name);

    // Cache state
    fs_state.was_toolbar_active = m_toolbarConfig.showToolbar;
    fs_state.old_clamped_w = v_env.clamped_screen_dim_w;
    fs_state.old_clamped_h = v_env.clamped_screen_dim_h;
    fs_state.fullscreen_win = win;

    // Kill toolbar!
    SetToolbarActive(false);

    v_env.clamped_screen_dim_w = v_env.screen_dim_w;
    v_env.clamped_screen_dim_h = v_env.screen_dim_h;

    // Force this window to full control
    win->set_layer(255);
    win->set_position(0, 0, false);
    win->set_size(v_env.screen_dim_w, v_env.screen_dim_h);
    win->Initialcfg.borderless = true;
    win->Currentcfg.borderless = true;
    win->dirty = true;
    // Stop all other windows from drawing

    for (auto& w : windows) {
        if (w && w != win) w->HaltDrawing();
    }
    vTaskDelay(pdMS_TO_TICKS(50));   // was 30




    // **Aggressive cleanup**
    PruneDeadWindows();
    windows_repositioned = true;
    ResetRepositioning();

    // Remove all other windows temporarily (except this one)
    for (auto it = windows.begin(); it != windows.end(); ) {
        if (*it && *it != win) {
            (*it)->IsWindowShown = false;
            it = windows.erase(it);
        } else {
            ++it;
        }
    }

    RepositionAllWindows();   // should be harmless now
    win->WinDraw();

    ESP_LOGI(TAG, "Fullscreen lock active. Other windows pruned.");
}

void WindowManager::restore_from_fullscreen()
{
    if (!fs_state.fullscreen_win) return;

    ESP_LOGI(TAG, "Restoring from fullscreen");

    v_env.clamped_screen_dim_w = fs_state.old_clamped_w;
    v_env.clamped_screen_dim_h = fs_state.old_clamped_h;

    SetToolbarActive(fs_state.was_toolbar_active);

    // Force full redraw after restore
    for (auto& win : windows) {
        if (win) {
            win->dirty = true;
            win->ResumeDrawing();
        }
    }
    g_display_dirty = true;

    fs_state.fullscreen_win.reset();
    windows_repositioned = false;   // re-enable normal behavior
    ResetRepositioning();
    RepositionAllWindows();


}

void WindowManager::ResetTheRepositioning() {
    windows_repositioned = false;
    ResetRepositioning();  // your existing one
}

void WindowManager::UpdateToolbar() {
    if (!m_toolbarConfig.showToolbar) return;
    
    uint64_t now = esp_timer_get_time();
    uint64_t update_interval_us = 500000 / m_toolbarConfig.tb_update_hz;
    
    if (now - last_toolbar_update >= update_interval_us || tb_dirty) {
        DrawToolBar();
        last_toolbar_update = now;
    }
}


void WindowManager::SortWindowsByLayer() {
    std::sort(windows.begin(), windows.end(),
        [](const std::shared_ptr<Window>& a, const std::shared_ptr<Window>& b) {
            if (!a) return false;
            if (!b) return true;
            return a->Initialcfg.Layer < b->Initialcfg.Layer;
        });
}

void WindowManager::DebugPrintWindowDOM() const {
    ESP_LOGI(TAG, "[debug print]");
    ESP_LOGI(TAG, "<window manager>");
    ESP_LOGI(TAG, "<bar>");  // placeholder for toolbar if you want

    for (const auto& win : windows) {
        if (!win || !win->IsWindowShown) continue;

        ESP_LOGI(TAG, ">window \"%s\"", win->Currentcfg.name);

        // Background info
        const char* tileType = "unknown";
        switch (win->Currentcfg.backgroundType) {
            case BgFillType::Solid:              tileType = "solid"; break;
            case BgFillType::GradientVertical:   tileType = "gradient_v"; break;
            case BgFillType::GradientHorizontal: tileType = "gradient_h"; break;
            case BgFillType::Checkerboard:       tileType = "checkerboard"; break;
            case BgFillType::Noise:              tileType = "noise"; break;
            case BgFillType::Diagonal_lines:     tileType = "diagonal"; break;
            case BgFillType::Transparent:        tileType = "transparent"; break;
            case BgFillType::waves:              tileType = "waves"; break;
            case BgFillType::triangles:          tileType = "triangles"; break;
            case BgFillType::dots:               tileType = "dots"; break;
            default: break;
        }
        ESP_LOGI(TAG, ".backgroundtile: \"%s\"", tileType);

        // Canvas(es) - currently one per window
        if (auto canvas = win->GetCanvas()) {
            ESP_LOGI(TAG, ">>canvas \"default_canvas\"");  // name it as you prefer
            // TODO: dump objects in canvas if you expose shape list
            ESP_LOGI(TAG, ">>>objects in canvas: %d shapes", 
                     canvas->GetShapeBuffer() ? canvas->GetShapeBuffer()->count : 0);
        }

        // Focused / highlighted state
        ESP_LOGI(TAG, "  focused=%d  highlighted=%d  animated_border=%d", 
                 (win->window_highlighted ? 1 : 0),
                 (win->window_highlighted ? 1 : 0),
                 (win->win_internal_optionsBitmask & WIN_OPT_ANIMATED_BORDER ? 1 : 0));
    }
}


// Update UpdateAll to handle toolbar with cooperative yielding
void WindowManager::UpdateAll(bool force, bool ToolbarUpdate, bool repositionWindows, bool draw_toolbar_ontop)
{


 ToolbarUpdate=false;
 draw_toolbar_ontop=false;
 //fuck off, i'm not dealing with this shit





    static uint32_t last_update_ms = 0;
    uint32_t now_ms = esp_timer_get_time() / 1000;

    const uint32_t min_frame_interval_ms = 33;   // ~30 FPS target

    if (!force && (now_ms - last_update_ms) < min_frame_interval_ms) {
        return;
    }

    last_update_ms = now_ms;

    bool did_any_drawing = false;

    // === CRITICAL: Skip repositioning if we are in fullscreen mode ===
    bool is_fullscreen_active = (fs_state.fullscreen_win != nullptr);
    if (repositionWindows && !windows_repositioned && !is_fullscreen_active) {
        RepositionAllWindows();
        tb_dirty = true;
        did_any_drawing = true;
    }

    // Remove dead windows
    for (auto it = windows.begin(); it != windows.end(); ) {
        if (!*it || !(*it)->IsWindowShown) {
            it = windows.erase(it);
            did_any_drawing = true;
            continue;
        }
        ++it;
    }

    // Sort by layer
    std::sort(windows.begin(), windows.end(),
        [](const std::shared_ptr<Window>& a, const std::shared_ptr<Window>& b) {
            if (!a) return false;
            if (!b) return true;
            return a->Initialcfg.Layer < b->Initialcfg.Layer;
        });

    // Draw toolbar BEHIND windows
    if (!draw_toolbar_ontop && ToolbarUpdate && m_toolbarConfig.showToolbar) {
        DrawToolBar();
        vTaskDelay(pdMS_TO_TICKS(1));
        did_any_drawing = true;
    }

    // Draw windows
    int window_count = 0;
    for (auto& win : windows) {
        if (!win) continue;

        if (force) win->enable_refresh_override = true;

        esp_task_wdt_reset();
        win->WinDraw();
        esp_task_wdt_reset();

        if (win->dirty) {
            did_any_drawing = true;
            win->dirty = false;        // reset here after drawing
        }

        window_count++;
        if ((window_count & 0x03) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    // Draw toolbar ON TOP
    if (draw_toolbar_ontop && ToolbarUpdate && m_toolbarConfig.showToolbar) {
        DrawToolBar();
        vTaskDelay(pdMS_TO_TICKS(1));
        did_any_drawing = true;
    }

    // === Notify display task only when needed ===
    if (did_any_drawing || force || tb_dirty) {
        g_display_dirty = true;
        if (core2TaskHandle) {
            xTaskNotifyGive(core2TaskHandle);
        }
    }

    tb_dirty = false;   // reset toolbar dirty flag
}












//WARNING: THIS FUNCTION HAS SIGNIFICANT HISTORY OF CAUSING WDT TIMEOUT FAILURES! THIS IS HEAVY! IF YOU ARE USING REALLY IMPORTANT STUFF WE MIGHT WANT TO USE HEADLESS MODE!

//6/24/2026 attempted fix; was too heavy with interval timing pushing all the time
void core2_push(void* pv)
{
    esp_task_wdt_add(NULL);

    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 8000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true
    };

    esp_task_wdt_reconfigure(&wdt_config);

    while (!(v_env.headless)){
        //while 1 loop, but only if it has "head" display attatched. no sense if not, really
    //this method waits for the core 1 task to say push instead of automatically doing so continuously
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        esp_task_wdt_reset();

        display_framebuffer(true, false);

        g_display_dirty = false;

        xSemaphoreGive(g_display_mutex);
    }

    esp_task_wdt_reset();
}
    //old framegen type disabled for now
    /*
    while (1)
    {
        uint8_t target_fps = v_env.UseFrameThrottle
            ? v_env.framethrottle_target
            : v_env.fpsTarget;

        if (target_fps == 0)
            target_fps = 1; // avoid divide-by-zero

        const uint32_t target_frame_interval_ms = 1000 / target_fps;

        // Wait for notification OR timeout
        ulTaskNotifyTake(
            pdTRUE,
            pdMS_TO_TICKS(target_frame_interval_ms)
        );

        uint32_t now = esp_timer_get_time() / 1000;

        // Don't push faster than target FPS
        if ((now - g_last_display_time) < target_frame_interval_ms)
        {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        if (!g_display_dirty)
        {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            esp_task_wdt_reset();

            display_framebuffer(true, false);

            g_last_display_time = now;
            g_display_dirty = false;

            xSemaphoreGive(g_display_mutex);
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1));
    }*/

}

void launchTHESTUPIDMOTHERFUCKINGPEICEOFSHITDISPLAYPUSHTASKFUCKYOU(){
xTaskCreatePinnedToCore(core2_push, "core2", 8192, NULL, 5, &core2TaskHandle, 0);
}
