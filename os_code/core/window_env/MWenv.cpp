
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
#include "os_code/core/rShell/enviroment/env_vars.h"

static const char *TAG = "MWenv"; 


//backgroundfill will use a seperate small buffer to avoid regenerating complex patterns or images, fortunately it's smaller than the actual screen size, and loops, so we'll just render one tile and translate it from position a to b in memory
//i think we'll have to transfer the blocks over to the adjacent position in memory, therefore redrawing it over text
//i'll have to account for text rotation, and store this in psram as bitmap segments
//i hate to do this but i'll encapsulate the background tile in an object

PsramBackgroundTile::PsramBackgroundTile(uint16_t tileSizeX, uint16_t tileSizeY) {
    if (allocated) return;

    pbt_cfg.tileSize_x = tileSizeX;
    pbt_cfg.tileSize_y = tileSizeY;

    size_t sz = (size_t)tileSizeX * tileSizeY * sizeof(uint16_t);
    ESP_LOGI("PsramBG", "Allocating %u×%u tile (%u bytes)", tileSizeX, tileSizeY, sz);

    pseudoframebuffer = (uint16_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);

    if (pseudoframebuffer) {
        allocated = true;
        ESP_LOGI("PsramBG", "PSRAM tile allocated at %p", pseudoframebuffer);
    } else {
        ESP_LOGE("PsramBG", "Failed to allocate background tile!");
    }
}

PsramBackgroundTile::~PsramBackgroundTile() {
    if (pseudoframebuffer) {
        heap_caps_free(pseudoframebuffer);
        pseudoframebuffer = nullptr;
        allocated = false;
    }
}

void PsramBackgroundTile::generate_pattern(BgFillType type, uint16_t primary, uint16_t secondary) {
    if (!allocated || !pseudoframebuffer) return;

    primaryColor = primary;
    secondaryColor = secondary;
    uint16_t* bmp = pseudoframebuffer;
    const uint16_t SX = pbt_cfg.tileSize_x;
    const uint16_t SY = pbt_cfg.tileSize_y;
    //type=BgFillType::waves;
    ESP_LOGE("PsramBG", "colors %u %u", primary, secondary);

    switch (type) {

        case BgFillType::Solid:
            for (uint16_t i = 0; i < SX * SY; ++i) 
                bmp[i] = primary;
            break;

        case BgFillType::GradientVertical:
            for (uint16_t y = 0; y < SY; ++y) {
                uint16_t c = primary + ((secondary - primary) * y) / (SY - 1);
                for (uint16_t x = 0; x < SX; ++x) 
                    bmp[y * SX + x] = c;
            }
            break;

        case BgFillType::GradientHorizontal:
            for (uint16_t y = 0; y < SY; ++y)
                for (uint16_t x = 0; x < SX; ++x) {
                    uint16_t c = primary + ((secondary - primary) * x) / (SX - 1);
                    bmp[y * SX + x] = c;
                }
            break;

        case BgFillType::Checkerboard:
            for (uint16_t y = 0; y < SY; ++y)
                for (uint16_t x = 0; x < SX; ++x)
                    bmp[y * SX + x] = ((x / 4 + y / 4) % 2 == 0) ? primary : secondary;
            break;

        case BgFillType::Noise:
            for (uint16_t i = 0; i < SX * SY; ++i)
                bmp[i] = (rand() & 1) ? primary : secondary;
            break;

        case BgFillType::Diagonal_lines:
            for (uint16_t y = 0; y < SY; ++y)
                for (uint16_t x = 0; x < SX; ++x)
                    bmp[y * SX + x] = ((x + y) % 8 < 4) ? primary : secondary;
            break;

        case BgFillType::Transparent:
            // Leave untouched (useful when you want to keep previous content 
            // or draw on top of another layer)
            break;

        case BgFillType::waves: //WARNING NOT EFFICIENT I THINK
        for (uint16_t y = 0; y < SY; ++y) {
            for (uint16_t x = 0; x < SX; ++x) {
        
                float t = (float)x * 0.19635f; // ~2π / 32
                int y_center = (int)(sinf(t) * 8 + 16);
        
                if (abs((int)y - y_center) <= 2)
                    bmp[y * SX + x] = secondary;
                else
                    bmp[y * SX + x] = primary;
            }
        }
            break;

        case BgFillType::triangles:
            for (uint16_t y = 0; y < SY; ++y)
                for (uint16_t x = 0; x < SX; ++x) {
                    // Simple repeating triangle / diagonal stripe pattern
                    int val = (x + y) % 16;
                    bmp[y * SX + x] = (val < 8) ? primary : secondary;
                }
            break;

        case BgFillType::dots:
            for (uint16_t y = 0; y < SY; ++y)
                for (uint16_t x = 0; x < SX; ++x) {
                    // Small dot pattern (every 4 pixels)
                    bool is_dot = ((x % 4 == 0) && (y % 4 == 0));
                    bmp[y * SX + x] = is_dot ? primary : secondary;
                }
            break;

        case BgFillType::count:
            // Should never reach here - added for completeness
            ESP_LOGW("PsramBG", "BgFillType::count should not be used as a pattern");
            [[fallthrough]];

        default:
            ESP_LOGW("PsramBG", "Unknown pattern %d – using Solid", (int)type);
            for (uint16_t i = 0; i < SX * SY; ++i) 
                bmp[i] = primary;
            break;
    }
}


void Window::setupBackgroundTile() {
    if (!bgTile) {
        bgTile = std::make_shared<PsramBackgroundTile>(32, 32);
    }

    bgTile->pbt_cfg.fill_type = win_backgroundpattern;
    bgTile->generate_pattern(win_backgroundpattern, bgPrimaryColor, bgSecondaryColor);

    ESP_LOGI(TAG, "Background tile generated: pattern %d, primary=0x%04X, secondary=0x%04X",
             (int)win_backgroundpattern, bgPrimaryColor, bgSecondaryColor);
}

// Helper – copies one tile into the framebuffer with rotation
static void blit_tile(                 // renamed for clarity
    uint16_t targetX, uint16_t targetY,
    uint16_t* framebuffer,
    uint16_t* tileBuffer,
    uint16_t tileW, uint16_t tileH)
{
    for (uint16_t ty = 0; ty < tileH; ++ty) {
        for (uint16_t tx = 0; tx < tileW; ++tx) {
            int sx = targetX + tx;
            int sy = targetY + ty;

            if (sx < 0 || sy < 0 || sx >= SCREEN_W || sy >= SCREEN_H)
                continue;

            uint16_t color = tileBuffer[ty * tileW + tx];
            framebuffer[sy * SCREEN_W + sx] = color;
        }
    }
}
 //pushes origin buffer to target framebuffer at position xy with rotation
//rot quadrant is the cartesian quadrant the tile is meant to be rotated into when placed. typically just follows the window
//start x is rotated by rot quadrant before it begins

static void blit_tile_clipped(
    uint16_t targetX, uint16_t targetY,           // where the tile top-left would go on screen
    uint16_t clipX,   uint16_t clipY,
    uint16_t clipW,   uint16_t clipH,             // window's physical bounding box
    uint16_t* framebuffer,
    uint16_t* tileBuffer,
    uint16_t tileW,   uint16_t tileH)
{
    // Compute overlapping rectangle between the placed tile and the window clip area
    int left   = std::max(static_cast<int>(targetX), static_cast<int>(clipX));
    int top    = std::max(static_cast<int>(targetY), static_cast<int>(clipY));
    int right  = std::min(static_cast<int>(targetX + tileW), static_cast<int>(clipX + clipW));
    int bottom = std::min(static_cast<int>(targetY + tileH), static_cast<int>(clipY + clipH));

    if (left >= right || top >= bottom) return;

    uint16_t src_x = left - targetX;
    uint16_t src_y = top  - targetY;
    uint16_t copy_w = right - left;
    uint16_t copy_h = bottom - top;

    for (uint16_t dy = 0; dy < copy_h; ++dy) {
        for (uint16_t dx = 0; dx < copy_w; ++dx) {
            int sx = left + dx;
            int sy = top + dy;

            // still respect screen edges
            if (sx < 0 || sy < 0 || sx >= SCREEN_W || sy >= SCREEN_H) continue;

            uint16_t color = tileBuffer[(src_y + dy) * tileW + (src_x + dx)];
            framebuffer[sy * SCREEN_W + sx] = color;
        }
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
      Currentcfg(cfg)
{
    // Make sure name is null-terminated
    Currentcfg.name[sizeof(Currentcfg.name) - 1] = '\0';

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
    fontdata w_font_info=ft_AVR_classic_6x8; //we'll just set this here as a default... 
    // Create small fixed-size tile (32×32 is perfect for repeating patterns)
    bgTile = std::make_shared<PsramBackgroundTile>(32, 32);

    // ✅ FIXED: Calculate logicalW and logicalH using the same formula as WinDraw()
    calculateLogicalDimensions();
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

    // Reuse the same calculation (DRY principle)
    calculateLogicalDimensions();

    const int rot   = wi_sizing.rotation & 3;
    const int rawW  = wi_sizing.Width;
    const int rawH  = wi_sizing.Height;

    const int physW = logicalW;   // now consistent
    const int physH = logicalH;

    // === CRITICAL: logical window (0,0) must be exactly at screen (Xpos, Ypos) ===
    int offsetX, offsetY;
    rotPointLocal(0, 0, rawW, rawH, rot, offsetX, offsetY);

    int physX = wi_sizing.Xpos - offsetX;
    int physY = wi_sizing.Ypos - offsetY;

    // Clamp to screen
    
    physX = std::max(0, std::min(physX, v_env.screen_dim_w - physW));
    physY = std::max(0, std::min(physY, v_env.screen_dim_h - physH));

    ESP_LOGI(TAG, "WinDraw rot=%d | logical(%dx%d) @ (%d,%d) → phys(%d,%d %dx%d)",
             rot, rawW, rawH, wi_sizing.Xpos, wi_sizing.Ypos, physX, physY, physW, physH);

    // === BACKGROUND FILL ===
    uint16_t clipX = physX;
    uint16_t clipY = physY;
    uint16_t clipW = physW;
    uint16_t clipH = physH;
 
             if (win_backgroundpattern == BgFillType::Solid) {
                 fb_rect(true, 1, physX, physY, physW, physH,
                         win_internal_color_background, win_internal_color_border);
             } 
             else {
                 if (!bgTile || bgTile->pbt_cfg.fill_type != win_backgroundpattern ||
                     bgTile->primaryColor != bgPrimaryColor ||
                     bgTile->secondaryColor != bgSecondaryColor) {
                     setupBackgroundTile();
                 }
 
                 if (bgTile && bgTile->allocated) {
                     const uint16_t TW = bgTile->pbt_cfg.tileSize_x;
                     const uint16_t TH = bgTile->pbt_cfg.tileSize_y;
 
                     // Tile in LOGICAL window space
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
                     // fallback
                     fb_rect(true, 1, physX, physY, physW, physH,
                             win_internal_color_background, win_internal_color_border);
                 }
             }
 
             // === TOP BAR + BORDER (new) ===
             if (win_internal_optionsBitmask & WIN_OPT_SHOW_TOP_BAR_MENU ||
                 Currentcfg.ShowNameAtTopOfWindow) {
 
                 const int bar_height = 24;  // adjust to taste
 
                 // Top bar background (always at the physical top of this window)
                 fb_rect(true, 1,
                         physX, physY, physW, bar_height,
                         win_internal_color_border,  // bar color
                         win_internal_color_border);
 
                 // Title text (centered or left-aligned)
                 fb_draw_text(physX + 6, physY + 4, physW - 40, //why is this hardcoded? 
                              Currentcfg.name,
                              0xFFFF, 1,
                              0, true, 0x0000, 40, 
                              w_font_info);
             }
 
             if (!Currentcfg.borderless) {
                 // outer border (now drawn AFTER top bar so it doesn't get overwritten)
                 fb_rect(false, 1, physX, physY, physW, physH, 0x0000, win_internal_color_border);
             }

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
             12, Tstate.highlight_bg, win_internal_color_background,
              999, w_font_info); //i don't think i'm using the highlight command right

            curLX += txt.length() * (w_font_info.fcs.x) * Tstate.size;
            if (curLX >= rawW - 4) {
                curLX = 2;
                curLY += (w_font_info.fcs.y) * last_line_height + 4;
            }
            last_line_height = Tstate.size;
            break;
        }

        case TagType::LineBreak:
            curLX = 2;
            curLY += w_font_info.fcs.y * last_line_height + 4;
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
WindowManager::WindowManager(){
    //DON'T REALLY need to do anything outside of initialize the window manager freerots task
}
WindowManager::~WindowManager(){
    //i have no idea what you would do here
}

void WindowManager::UpdateAll() {
    // Remove expired weak_ptrs and update active windows
    for (auto it = windows.begin(); it != windows.end(); ) {
        if (!*it || !(*it)->IsWindowShown) {
            ESP_LOGD(TAG, "WindowManager: Removing dead window");
            it = windows.erase(it);
            continue;
        }

        (*it)->WinDraw();        // This is what actually draws
        ++it;
    }
}

bool WindowManager::PruneDeadWindows() {
    /*
    windows.erase(std::remove_if(windows.begin(), windows.end(),
        [](const auto& w) {
            return !w || !w->IsWindowShown;
        }), windows.end());*/
        return false; //get out my way for aminute
}

bool WindowManager::registerWindow(std::shared_ptr<Window> window) {
    if (!window) return false;

    windows.push_back(window);
    return true;
}