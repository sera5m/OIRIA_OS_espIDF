
#include <stdint.h>

#include <string>
#include <memory>
#include <sstream>
#include <algorithm>
#include <variant>//unions for the code
#include "code_stuff/types.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include <memory>
#include <math.h>
#include "hardware/wiring/wiring.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "rom/cache.h"
#include <string.h>
#include <math.h>
#include "hardware/drivers/abstraction_layers/al_scr.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "os_code/core/window_env/MWenv.hpp"
#include "esp_timer.h"
#include "os_code\core\window_env\wenv_basicThemes.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include <esp_heap_caps.h>

#include "hardware/drivers/psram_std/psram_std.hpp" //my custom work for psram stdd things
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
// Custom allocator for std::string that prefers PSRAM
#include "esp_log.h"

static const char* TAG = "WindowDraw";

Window::Window(const WindowCfg& cfg, const std::string& initialContent)
    : content(stdpsram::String(initialContent.begin(), initialContent.end())),
      Initialcfg(cfg),
      Currentcfg(cfg)
{
	
    // Make sure name is null-terminated
    Currentcfg.name[sizeof(Currentcfg.name) - 1] = '\0';

    // Sync sizing & colors
    wi_sizing.Xpos   = Currentcfg.Posx;
    wi_sizing.Ypos   = Currentcfg.Posy;
    wi_sizing.Width  = Currentcfg.win_width;
    wi_sizing.Height = Currentcfg.win_height;
	wi_sizing.rotation=Currentcfg.win_rotation;
    win_internal_color_background = Currentcfg.BgColor;
    win_internal_color_border     = Currentcfg.BorderColor;
    win_internal_color_text       = Currentcfg.WinTextColor;
    win_internal_textsize_mult    = Currentcfg.TextSizeMult;

    UpdateTickRate = Currentcfg.UpdateRate;
    
    win_internal_optionsBitmask=Currentcfg.optionsbitmask;
    
    //vec2_ui16t LocalXYPos={0,0}; //needs to use rotpointlocal to set this uint16 vector...idk seems fine i'll leave this
    //vec2_ui16t internalWH=//needs to have the rotation set and fixed here precomputed
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

// Helper function (add to MWenv.cpp or a utils header)
static int16_t parse_int(const stdpsram::String& str, int base) {
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
// MWenv.cpp – add these right after includes or before tokenize

// MWenv.cpp
// Add this block right after your includes (or before tokenize)

// Anonymous namespace = visible only in this .cpp file


 int safe_parse_int(const stdpsram::String& str, int default_val = 0) {
    if (str.empty()) return default_val;

    int sign = 1;
    size_t i = 0;

    if (!str.empty() && str[0] == '-') { sign = -1; ++i; }
    if (!str.empty() && str[0] == '+') { ++i; }

    int result = 0;
    bool digits_found = false;

    while (i < str.size()) {
        char c = str[i++];
        if (c < '0' || c > '9') break;
        result = result * 10 + (c - '0');
        digits_found = true;
    }

    return digits_found ? result * sign : default_val;
}

 uint16_t safe_parse_color(const stdpsram::String& str, uint16_t default_val = 0xFFFF) {
    if (str.empty()) return default_val;

    size_t start = 0;
    int base = 10;

    // Check for 0x / 0X prefix
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

        // Early overflow protection (16-bit color)
        if (result > 0xFFFF) return default_val;
    }

    return digits_found ? static_cast<uint16_t>(result) : default_val;
}

  // end anonymous namespace  // end anonymous namespace

// ──────────────────────────────────────────────
// Main tokenize function – cleaner structure
// ──────────────────────────────────────────────
stdpsram::Vector<TextChunk> Window::tokenize(const stdpsram::String& s) {
    stdpsram::Vector<TextChunk> chunks;
    if (s.empty()) {
        ESP_LOGI(TAG, "Tokenize: empty input → empty chunks");
        return chunks;
    }

    stdpsram::String text_buffer;
    text_buffer.reserve(s.length() / 2 + 64);  // rough guess, helps perf

    auto flush = [&]() {
        if (!text_buffer.empty()) {
            chunks.emplace_back(TextChunk(std::move(text_buffer)));
            text_buffer.clear();
            ESP_LOGD(TAG, "Flushed text chunk, total chunks now: %zu", chunks.size());
        }
    };

    size_t i = 0;
    while (i < s.length()) {
        if (s[i] == '<' && i + 1 < s.length() && s[i + 1] == '|') {
            flush();

            size_t end = s.find("|>", i + 2);
            if (end == stdpsram::String::npos) {
                ESP_LOGW(TAG, "Unclosed tag at pos %zu → treat rest as text", i);
                text_buffer = s.substr(i);
                break;
            }

            stdpsram::String inside = s.substr(i + 2, end - i - 2);
            i = end + 2;

            if (inside.empty()) continue;

            ESP_LOGD(TAG, "Found tag: <%s>", inside.c_str());

            // ────────────────────── Short toggle tags ──────────────────────
            if (inside.length() == 1 || inside.length() == 2) {
                char first = inside[0];
                bool is_off = (inside.length() == 2 && inside[0] == '/');

                switch (first) {
                    case 'n':
                      if (inside == "n" ) {
                            chunks.emplace_back(TagType::LineBreak);
                            ESP_LOGD(TAG, "LineBreak");
                        }
                        break;

                    case 'u':
                        chunks.emplace_back(is_off ? TagType::UnderlineOff : TagType::UnderlineToggle);
                        ESP_LOGD(TAG, "Underline %s", is_off ? "OFF" : "ON");
                        break;

                    case 's':
                        chunks.emplace_back(is_off ? TagType::StrikethroughOff : TagType::StrikethroughToggle);
                        ESP_LOGD(TAG, "Strikethrough %s", is_off ? "OFF" : "ON");
                        break;

                    case 'b':
                        chunks.emplace_back(is_off ? TagType::BoldOff : TagType::BoldToggle);
                        ESP_LOGD(TAG, "Bold %s", is_off ? "OFF" : "ON");
                        break;

                    case 'i':
                        chunks.emplace_back(is_off ? TagType::ItalicOff : TagType::ItalicToggle);
                        ESP_LOGD(TAG, "Italic %s", is_off ? "OFF" : "ON");
                        break;

                    default:
                        ESP_LOGD(TAG, "Unknown short tag: %s → treat as text", inside.c_str());
                        text_buffer += inside;
                        break;
                }
                continue;
            }

            // ────────────────────── Value tags ──────────────────────
            if (inside.starts_with("color=")) {
                auto val = inside.substr(6);
                uint16_t col = safe_parse_color(val);
                chunks.emplace_back(TagType::ColorChange, ColorTag{col});
                ESP_LOGD(TAG, "Color → 0x%04x", col);
            }
            else if (inside.starts_with("size=")) {
                auto val = inside.substr(5);
                int sz = safe_parse_int(val, 1);
                if (sz >= 1 && sz <= 255) {
                    chunks.emplace_back(TagType::SizeChange, SizeTag{static_cast<uint8_t>(sz)});
                    ESP_LOGD(TAG, "Size → %d", sz);
                }
            }
            else if (inside.starts_with("pos=")) {
                size_t comma = inside.find(',', 4);
                if (comma != stdpsram::String::npos) {
                    auto x_str = inside.substr(4, comma - 4);
                    auto y_str = inside.substr(comma + 1);
                    int16_t x = safe_parse_int(x_str);
                    int16_t y = safe_parse_int(y_str);
                    chunks.emplace_back(TagType::PosChange, PosTag{x, y});
                    ESP_LOGD(TAG, "Pos → %d,%d", x, y);
                }
            }
            else if (inside.starts_with("hl=")) {
                auto val = inside.substr(3);
                uint16_t col = safe_parse_color(val, 0xFFFF);
                chunks.emplace_back(TagType::HighlightChange, HighlighterTag{col, true});
                ESP_LOGD(TAG, "Highlight ON color=0x%04x", col);
            }
            else {
                ESP_LOGD(TAG, "Unknown tag: %s → treat as literal text", inside.c_str());
                text_buffer += inside;
            }
        }
        else {
            text_buffer += s[i++];
        }
    }

    flush();
    ESP_LOGI(TAG, "Tokenization complete: %zu chunks", chunks.size());
    ESP_LOGI(TAG, "%s", s.c_str());
    return chunks;
}




// ──────────────────────────────────────────────
// COMPLETELY REWRITTEN WinDraw() – logical origin fixed at (Posx, Posy) for ALL rotations
// ──────────────────────────────────────────────
void Window::WinDraw() {
    if (!IsWindowShown) return;

    const int rot   = wi_sizing.rotation & 3;
    const int rawW  = wi_sizing.Width;
    const int rawH  = wi_sizing.Height;

    const int physW = (rot % 2 == 0) ? rawW : rawH;
    const int physH = (rot % 2 == 0) ? rawH : rawW;

    // === CRITICAL: logical window (0,0) must be exactly at screen (Xpos, Ypos) ===
    int offsetX, offsetY;
    rotPointLocal(0, 0, rawW, rawH, rot, offsetX, offsetY);

    int physX = wi_sizing.Xpos - offsetX;
    int physY = wi_sizing.Ypos - offsetY;

    // Clamp to screen (change these to your real screen size)
    const int screenW = 240;
    const int screenH = 320;
    physX = std::max(0, std::min(physX, screenW - physW));
    physY = std::max(0, std::min(physY, screenH - physH));

    ESP_LOGI(TAG, "WinDraw rot=%d | logical(%dx%d) @ (%d,%d) → phys(%d,%d %dx%d)",
             rot, rawW, rawH, wi_sizing.Xpos, wi_sizing.Ypos, physX, physY, physW, physH);

    // === Draw background + single border (this fixes the double-line bug) ===
    fb_rect(true,  0, physX, physY, physW, physH,
            win_internal_color_background, win_internal_color_border);

    if (!Currentcfg.borderless) {
        fb_rect(false, 1, physX, physY, physW, physH, 0x0000, win_internal_color_border);
    }

    // === Text rendering (unchanged, uses same rotation math) ===
    if (!isTokenized) {
        cachedChunks = tokenize(content);
        isTokenized = true;
    }

    Tstate.color         = win_internal_color_text;
    Tstate.size          = Currentcfg.TextSizeMult;
    Tstate.underline     = false;
    Tstate.strikethrough = false;
    Tstate.bold          = false;
    Tstate.italic        = false;
    Tstate.highlight_bg  = 0;

    const int text_rot_flag = rot * 4;
    fontcharsize font_glyph_size = font6x8;

    int curLX = 2;
    int curLY = 2;
    uint8_t last_line_height = Currentcfg.TextSizeMult;

    for (const auto& chunk : cachedChunks) {
        switch (chunk.kind) {
        case TagType::PlainText: {
            const auto& txt = std::get<stdpsram::String>(chunk.content);
            if (txt.empty()) break;

            int rx, ry;
            rotPointLocal(curLX, curLY, rawW, rawH, rot, rx, ry);
            int sx = physX + rx;
            int sy = physY + ry;

            

            fb_draw_ptext(text_rot_flag, 
            sx, sy, txt, Tstate.color, Tstate.size,
            
                          avrclassic_font6x8, 12, Tstate.highlight_bg, win_internal_color_background, 999, font_glyph_size); //i don't think i'm using the highlight command right

            curLX += txt.length() * font_glyph_size.x * Tstate.size;
            if (curLX >= rawW - 4) {
                curLX = 2;
                curLY += font_glyph_size.y * last_line_height + 4;
            }
            last_line_height = Tstate.size;
            break;
        }

        case TagType::LineBreak:
            curLX = 2;
            curLY += font_glyph_size.y * last_line_height + 4;
            break;

        case TagType::PosChange: {
            auto p = std::get<PosTag>(chunk.content);
            curLX = p.x; curLY = p.y;
            break;
        }

        case TagType::ColorChange:   Tstate.color = std::get<ColorTag>(chunk.content).value; break;
        case TagType::SizeChange: {
            int s = std::get<SizeTag>(chunk.content).value;
            if (s >= 1 && s <= 16) Tstate.size = s;
            break;
        }
        case TagType::HighlightChange: {
            auto h = std::get<HighlighterTag>(chunk.content);
            Tstate.highlight_bg = h.enabled ? h.color : 0;
            break;
        }
        case TagType::UnderlineToggle:    Tstate.underline = true;  break;
        case TagType::UnderlineOff:       Tstate.underline = false; break;
        case TagType::StrikethroughToggle:Tstate.strikethrough = true;  break;
        case TagType::StrikethroughOff:   Tstate.strikethrough = false; break;
        case TagType::BoldToggle:         Tstate.bold = true; break;
        case TagType::BoldOff:            Tstate.bold = false; break;
        case TagType::ItalicToggle:       Tstate.italic = true; break;
        case TagType::ItalicOff:          Tstate.italic = false; break;
        default: break;
        }
    }

    currentPhysX = physX;
    currentPhysY = physY;
    dirty = false;
    lastUpdateTime = esp_timer_get_time();
}

//guess who found out she needed to do this a lot after making the window system and working on other drivers
//i swear to god bruh


void Window::SetText(const std::string& newText) { //std string variant with explicit conversion
    content = stdpsram::String(newText.begin(), newText.end());  // force PSRAM copy
    isTokenized = false;
    dirty = true;
}

void Window::SetText(const stdpsram::String& newText) { //stdpsram variant with no conversion needed
    content=newText;  
    isTokenized = false;
    dirty = true;
}


//overloaded for std and stdpsram
void Window::AppendText(const stdpsram::String& moreText) {
    content.append(moreText);
    isTokenized = false;
    dirty = true;
}

void Window::AppendText(const std::string& moreText) { //overloaded stdstr, convert needed
    content.append(stdpsram::String(moreText.begin(), moreText.end()));
    isTokenized = false;
    dirty = true;
}

void Window::ClearText() {
    content.clear();
    cachedChunks.clear();
    isTokenized = false;
    dirty = true;
}

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



