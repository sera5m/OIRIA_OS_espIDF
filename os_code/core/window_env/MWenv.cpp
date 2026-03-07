
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




Window::Window(const WindowCfg& cfg, const std::string& initialContent)
    : content(initialContent),
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

    rotPointLocal(
        lx, ly,
        wi_sizing.Width,
        wi_sizing.Height,
        wi_sizing.rotation,
        rx, ry
    );

    sx = wi_sizing.Xpos + rx;
    sy = wi_sizing.Ypos + ry;
}



std::vector<TextChunk> Window::tokenize(const std::string& s) {
    std::vector<TextChunk> chunks;
    if (s.empty()) return chunks;

    size_t i = 0;
    std::string text_buffer;

    auto flush = [&]() {
        if (!text_buffer.empty()) {
            chunks.emplace_back(std::move(text_buffer));
            text_buffer.clear();
        }
    };

    while (i < s.size()) {
        if (s[i] == '<' && i + 1 < s.size() && s[i + 1] == '|') {  // <| opening
            flush();

            size_t end = s.find("|>", i + 2);
            if (end == std::string::npos) {
                // broken → treat rest as text
                text_buffer = s.substr(i);
                break;
            }

            std::string inside = s.substr(i + 2, end - i - 2);  // content between <| and |>
            i = end + 2;

            if (inside.empty()) continue;  // skip empty tags

            // ──────────────────────────────
            // Handle known tags
            // ──────────────────────────────

            if (inside == "n" || inside == "br") {
                chunks.emplace_back(TagType::LineBreak);
            }
            else if (inside == "u") {
                chunks.emplace_back(TagType::UnderlineToggle);
            }
            else if (inside == "/u") {
                chunks.emplace_back(TagType::UnderlineOff);
            }
            else if (inside == "s") {
                chunks.emplace_back(TagType::StrikethroughToggle);
            }
            else if (inside == "/s") {
                chunks.emplace_back(TagType::StrikethroughOff);
            }
            else if (inside == "b") {
                chunks.emplace_back(TagType::BoldToggle);
            }
            else if (inside == "/b") {
                chunks.emplace_back(TagType::BoldOff);
            }
            else if (inside == "i") {
                chunks.emplace_back(TagType::ItalicToggle);
            }
            else if (inside == "/i") {
                chunks.emplace_back(TagType::ItalicOff);
            }

            // Color: <|color=0xF800|> or <|color=63488|> (decimal also ok)
            else if (inside.starts_with("color=")) {
                std::string val = inside.substr(6);
                uint16_t col = 0xFFFF;  // fallback white

                if (!val.empty()) {
                    try {
                        if (val.starts_with("0x") || val.starts_with("0X")) {
                            col = static_cast<uint16_t>(std::stoul(val.substr(2), nullptr, 16));
                        } else {
                            col = static_cast<uint16_t>(std::stoul(val, nullptr, 0));  // auto-detect base
                        }
                    } catch (...) {
                        col = 0xFFFF;
                    }
                }
                chunks.emplace_back(TagType::ColorChange, ColorTag{col});
            }
            else if (inside == "/color") {
                chunks.emplace_back(TagType::ColorChange, ColorTag{0xFFFF});  // reset to default
            }

            // Size: <|size=3|> or <|size=1|>
            else if (inside.starts_with("size=")) {
                try {
                    int val = std::stoi(inside.substr(5));
                    if (val >= 1 && val <= 255) {
                        chunks.emplace_back(TagType::SizeChange, SizeTag{static_cast<uint8_t>(val)});
                    }
                } catch (...) {}
            }
            else if (inside == "/size") {
                chunks.emplace_back(TagType::SizeChange, SizeTag{1});
            }

            // Position: <|pos=10,40|>
            else if (inside.starts_with("pos=")) {
                size_t comma = inside.find(',', 4);
                if (comma != std::string::npos) {
                    try {
                        int16_t x = std::stoi(inside.substr(4, comma - 4));
                        int16_t y = std::stoi(inside.substr(comma + 1));
                        chunks.emplace_back(TagType::PosChange, PosTag{x, y});
                    } catch (...) {}
                }
            }

            // Highlight: <|hl=0xFFFF|> or <|hl|> = toggle on/off
            else if (inside.starts_with("hl=")) {
                std::string val = inside.substr(3);
                uint16_t col = 0xFFFF;
                try {
                    if (val.starts_with("0x")) {
                        col = static_cast<uint16_t>(std::stoul(val.substr(2), nullptr, 16));
                    } else {
                        col = static_cast<uint16_t>(std::stoul(val, nullptr, 0));
                    }
                } catch (...) {}
                chunks.emplace_back(TagType::HighlightChange, HighlighterTag{col, true});
            }
            else if (inside == "hl" || inside == "/hl") {
                chunks.emplace_back(TagType::HighlightChange, HighlighterTag{0x0000, false});
            }

            // Unknown → pass through as literal text (don't add markup)
            else {
                text_buffer += inside;
            }
        }
        else {
            text_buffer += s[i++];
        }
    }

    flush();
    return chunks;
}

void Window::WinDraw() {
    if (!IsWindowShown) return;

    const int rot = wi_sizing.rotation & 3;
    const int W = wi_sizing.Width;
    const int H = wi_sizing.Height;
    const int drawW = (rot % 2) ? H : W;
    const int drawH = (rot % 2) ? W : H;
    const fontcharsize font_glyph_size = font6x8;

    fb_rect(true, 0, wi_sizing.Xpos, wi_sizing.Ypos, drawW, drawH,
            win_internal_color_background, win_internal_color_border);

    if (!Currentcfg.borderless) {
        fb_rect(false, 1, wi_sizing.Xpos, wi_sizing.Ypos, drawW, drawH,
                0x0000, win_internal_color_border);
    }

    struct TextState {
        uint16_t color = 0xFFFF;
        uint8_t  size  = 1;
        bool     underline     = false;
        bool     strikethrough = false;
        bool     bold          = false;    // placeholder — could switch font later
        bool     italic        = false;    // placeholder — could shear or switch font
        uint16_t highlight_bg  = 0;        // 0 = no highlight
    };

    TextState state;
    state.color = win_internal_color_text;
    state.size  = Currentcfg.TextSizeMult;

    int curLX = 2;
    int curLY = 2;

    uint8_t last_line_height_mult = state.size;
    uint8_t max_size_this_line    = state.size;

    if (!isTokenized) {
    cachedChunks = tokenize(content);
    isTokenized = true;
}

auto& chunks = cachedChunks;

    for (const auto& chunk : chunks) {
        switch (chunk.kind) {

        case TagType::PlainText: {
            const std::string& txt = std::get<std::string>(chunk.content);
            if (txt.empty()) break;

            int screenX, screenY;
            LocalToScreen(curLX, curLY, screenX, screenY);

            bool is_vertical = (rot % 2) == 1;

            int char_advance_px = is_vertical 
                ? font_glyph_size.y * state.size 
                : font_glyph_size.x * state.size;

            int remaining_px = drawW - (is_vertical ? curLY : curLX) - 4;

            uint16_t max_chars = 0;
            if (char_advance_px > 0) {
                max_chars = remaining_px / char_advance_px;
            }

            if (state.size < max_size_this_line) {
                int cons_advance = is_vertical 
                    ? font_glyph_size.y * max_size_this_line 
                    : font_glyph_size.x * max_size_this_line;
                uint16_t cons_max = remaining_px / cons_advance;
                max_chars = std::min(max_chars, cons_max);
            }

            if (max_chars < 8) max_chars = 999;

            // ──────────────────────────────
            // 1. Draw highlight background (if active)
            // ──────────────────────────────
            if (state.highlight_bg != 0) {
                int bg_x0 = curLX;
                int bg_y0 = curLY;
                int bg_x1 = curLX + txt.length() * font_glyph_size.x * state.size;
                int bg_y1 = curLY + font_glyph_size.y * state.size;

                int sx0, sy0, sx1, sy1;
                LocalToScreen(bg_x0, bg_y0, sx0, sy0);
                LocalToScreen(bg_x1, bg_y1, sx1, sy1);

                // Make sure coords are ordered correctly
                int left   = std::min(sx0, sx1);
                int top    = std::min(sy0, sy1);
                int right  = std::max(sx0, sx1);
                int bottom = std::max(sy0, sy1);

                fb_rect(true, 0, left, top, right - left + 1, bottom - top + 1,
                        state.highlight_bg, state.highlight_bg);  // filled rect
            }

            // ──────────────────────────────
            // 2. Draw the actual text
            // ──────────────────────────────
            fb_draw_text(
                rot * 4,
                screenX, screenY,
                txt.c_str(),
                state.color,
                state.size,
                avrclassic_font6x8,
                0,
                false,
                win_internal_color_background,
                max_chars,
                font_glyph_size
            );

            // ──────────────────────────────
            // 3. Draw underline / strikethrough if active
            // ──────────────────────────────
            if (state.underline || state.strikethrough) {
                int line_y_offset;
                if (state.underline) {
                    line_y_offset = font_glyph_size.y * state.size - 1;  // bottom
                } else {
                    line_y_offset = (font_glyph_size.y * state.size) / 2; // middle for strikethrough
                }

                int ul_y = curLY + line_y_offset;
                int ul_x_start = curLX;
                int ul_x_end   = curLX + txt.length() * font_glyph_size.x * state.size;

                int sx0, sy0, sx1, sy1;
                LocalToScreen(ul_x_start, ul_y, sx0, sy0);
                LocalToScreen(ul_x_end,   ul_y, sx1, sy1);

                // Draw horizontal line (handles rotation via screen coords)
                fb_line(sx0, sy0, sx1, sy1, state.color);
            }

            // Advance
            int this_chunk_advance = static_cast<int>(txt.length()) * (font_glyph_size.x * state.size);
            curLX += this_chunk_advance;

            if (state.size > max_size_this_line) {
                max_size_this_line = state.size;
            }

            last_line_height_mult = state.size;

            // Force wrap if overrun
            if (Currentcfg.WrapText && curLX >= drawW - 10) {
                curLX = 2;
                curLY += (font_glyph_size.y * last_line_height_mult) + 2;
                max_size_this_line = state.size;
            }

            break;
        }

        case TagType::LineBreak:
            curLX = 2;
            curLY += (font_glyph_size.y * last_line_height_mult) + 2;
            max_size_this_line = state.size;
            break;

        case TagType::PosChange: {
            auto pos = std::get<PosTag>(chunk.content);
            curLX = pos.x;
            curLY = pos.y;
            max_size_this_line = state.size;
            break;
        }

        case TagType::ColorChange: {
            auto c = std::get<ColorTag>(chunk.content);
            state.color = c.value;
            break;
        }

        case TagType::SizeChange: {
            auto sz = std::get<SizeTag>(chunk.content);
            state.size = sz.value;
            break;
        }

        case TagType::HighlightChange: {
            auto hl = std::get<HighlighterTag>(chunk.content);
            state.highlight_bg = hl.enabled ? hl.color : 0;
            break;
        }

        // ──────────────────────────────
        // Style toggles
        // ──────────────────────────────
        case TagType::UnderlineToggle:
            state.underline = true;
            break;
        case TagType::UnderlineOff:
            state.underline = false;
            break;

        case TagType::StrikethroughToggle:
            state.strikethrough = true;
            break;
        case TagType::StrikethroughOff:
            state.strikethrough = false;
            break;

        case TagType::BoldToggle:
            state.bold = true;
            // TODO: if you have a bold font variant, switch font pointer here later
            break;
        case TagType::BoldOff:
            state.bold = false;
            break;

        case TagType::ItalicToggle:
            state.italic = true;
            // TODO: if you have italic font or want shear transform, add here
            break;
        case TagType::ItalicOff:
            state.italic = false;
            break;

        default:
            break;
        }
    }

    dirty = false;
    lastUpdateTime = esp_timer_get_time();
}
//guess who found out she needed to do this a lot after making the window system and working on other drivers
//i swear to god bruh

void Window::SetText(const std::string& newText) {
    content = newText;
    isTokenized = false;
    dirty = true;
}
    
void Window::AppendText(const std::string& moreText) {
    content += moreText;
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



