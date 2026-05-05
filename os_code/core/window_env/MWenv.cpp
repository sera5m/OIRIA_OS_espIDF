
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
#include "hardware/drivers/lcd/st7789v2/t_shapes.h"
#include "esp_task_wdt.h" 
//unrelated but i hate how every time ihave to type idf.py build flash monitor to check if the code works every time i make my 253st change of the day
//and idf also stands for isralie defense force, who are genocidal rapists and whatnot, it's a fun reminder 
static const char *TAG = "MWenv"; 

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

/////===============END PSRAM BACKGROUND TILE DATA

///////==canvas object

// ===================== CANVAS IMPLEMENTATION =====================
Canvas::Canvas(const CanvasCfg& cfg) 
    : m_cfg(cfg)
    , m_parentWindow(cfg.parentWindow)
    , m_shapeBuffer(nullptr)
    , m_maxShapes(FB_MAX_SHAPES)
    , m_dirty(true)
{
    // Allocate shape buffer on PSRAM
    m_shapeBuffer = (fb_shape_buffer_t*)heap_caps_malloc(
        sizeof(fb_shape_buffer_t), 
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    
    if (!m_shapeBuffer) {
        ESP_LOGE(TAG, "Failed to allocate shape buffer!");
        return;
    }
    
    // Initialize the buffer
    if (!fb_shapes_init(m_shapeBuffer, m_maxShapes)) {
        ESP_LOGE(TAG, "Failed to initialize shapes!");
        heap_caps_free(m_shapeBuffer);
        m_shapeBuffer = nullptr;
        return;
    }
    
    ESP_LOGI(TAG, "Canvas created with parent window at %p", m_parentWindow);
}

Canvas::~Canvas() {
    if (m_shapeBuffer) {
        fb_shapes_free(m_shapeBuffer);
        heap_caps_free(m_shapeBuffer);
        m_shapeBuffer = nullptr;
    }
}

void Canvas::Update(float deltaTime) {
    // Update any animated shapes or handle transformations
    // For now, just mark dirty if shapes changed
    if (m_dirty) {
        SortShapes();
        if (m_parentWindow) {
            m_parentWindow->dirty = true;
        }
        m_dirty = false;
    }
}

void Canvas::Draw() {
    if (!m_shapeBuffer || !m_parentWindow || !m_parentWindow->IsWindowShown) {
        return;
    }
    
    // Convert all shapes from canvas-local coordinates to screen coordinates
    for (uint16_t i = 0; i < m_shapeBuffer->count; i++) {
        fb_shape_t* shape = &m_shapeBuffer->shapes[i];
        if (!shape->shown) continue;
        
        // Convert bounds from canvas-local to screen coordinates
        s_bounds_16u screenBounds;
        int sx1, sy1, sx2, sy2;
        
        m_parentWindow->LocalToScreen(shape->bounds.x, shape->bounds.y, sx1, sy1);
        m_parentWindow->LocalToScreen(
            shape->bounds.x + shape->bounds.w, 
            shape->bounds.y + shape->bounds.h, 
            sx2, sy2
        );
        
        screenBounds.x = sx1;
        screenBounds.y = sy1;
        screenBounds.w = sx2 - sx1;
        screenBounds.h = sy2 - sy1;
        
        // Apply bounds checking (clamp to parent window)
        s_bounds_16u windowBounds = {
            .x = m_parentWindow->currentPhysX,
            .y = m_parentWindow->currentPhysY,
            .w = m_parentWindow->logicalW,
            .h = m_parentWindow->logicalH
        };
        
        screenBounds = ClampBoundsToParent(screenBounds, windowBounds);
        if (screenBounds.w <= 0 || screenBounds.h <= 0) continue;
        
        // Draw based on shape type
        switch ((fb_shape_type)shape->type) {
            case SHAPE_RECT:
                fb_rect(true, 1, 
                       screenBounds.x, screenBounds.y,
                       screenBounds.w, screenBounds.h,
                       shape->color, shape->color);
                break;
                
            case SHAPE_LINE:
                // For lines, bounds contains the two endpoints
                // bounds.x,y = start point, bounds.w,h = end point offset
                fb_line(screenBounds.x, screenBounds.y,
                       screenBounds.x + screenBounds.w,
                       screenBounds.y + screenBounds.h,
                       shape->color);
                break;
                
            case SHAPE_CIRCLE: {
                // For circles, bounds.x,y = center, bounds.w = radius
                int radius = screenBounds.w / 2;
                fb_circle(screenBounds.x + radius,
                         screenBounds.y + radius,
                         radius, plain, shape->color, shape->color);
                break;
            }
            
            case SHAPE_BITMAP:
                if (shape->data) {
                    fb_draw_bitmap(screenBounds.x, screenBounds.y,
                                  screenBounds.w, screenBounds.h,
                                  (const uint16_t*)shape->data);
                }
                break;
                
            case SHAPE_TEXT:
                if (shape->data) {
                    fb_draw_text(0, screenBounds.x, screenBounds.y,
                                (const char*)shape->data,
                                shape->color, 1, 0, true, 0x0000,
                                screenBounds.w, ft_AVR_classic_6x8);
                }
                break;
                
            default:
                break;
        }
    }
}

fb_shape_t* Canvas::AddShape(fb_shape_type type, s_bounds_16u bounds, 
                              uint16_t color, uint8_t layer) {
    if (!m_shapeBuffer) return nullptr;
    
    // Bounds check - ensure shape is within canvas
    s_bounds_16u clampedBounds = bounds;
    if (clampedBounds.x < 0) {
        clampedBounds.w += clampedBounds.x;
        clampedBounds.x = 0;
    }
    if (clampedBounds.y < 0) {
        clampedBounds.h += clampedBounds.y;
        clampedBounds.y = 0;
    }
    if (clampedBounds.x + clampedBounds.w > m_cfg.width) {
        clampedBounds.w = m_cfg.width - clampedBounds.x;
    }
    if (clampedBounds.y + clampedBounds.h > m_cfg.height) {
        clampedBounds.h = m_cfg.height - clampedBounds.y;
    }
    
    if (clampedBounds.w <= 0 || clampedBounds.h <= 0) {
        ESP_LOGW(TAG, "Shape bounds invalid after clamping");
        return nullptr;
    }
    
    fb_shape_t* shape = fb_shape_add(m_shapeBuffer, type, clampedBounds, color, layer);
    if (shape) {
        m_dirty = true;
        ESP_LOGI(TAG, "Added shape type=%d at (%d,%d) size=%dx%d", 
                 type, bounds.x, bounds.y, bounds.w, bounds.h);
    }
    
    return shape;
}

void Canvas::RemoveShape(uint16_t index) {
    if (!m_shapeBuffer || index >= m_shapeBuffer->count) return;
    
    // Shift all shapes after index left by one
    for (uint16_t i = index; i < m_shapeBuffer->count - 1; i++) {
        m_shapeBuffer->shapes[i] = m_shapeBuffer->shapes[i + 1];
    }
    
    m_shapeBuffer->count--;
    m_dirty = true;
}

void Canvas::ClearShapes() {
    if (!m_shapeBuffer) return;
    m_shapeBuffer->count = 0;
    for (int i = 0; i < FB_MAX_LAYERS; i++) {
        m_shapeBuffer->layer_counts[i] = 0;
        m_shapeBuffer->layer_offsets[i] = 0;
    }
    m_dirty = true;
}

void Canvas::SortShapes() {
    if (!m_shapeBuffer) return;
    fb_shapes_Fsort_by_layer(m_shapeBuffer);  // Use the fixed version
}

void Canvas::SetShapeVisible(uint16_t index, bool visible) {
    if (!m_shapeBuffer || index >= m_shapeBuffer->count) return;
    m_shapeBuffer->shapes[index].shown = visible;
    m_dirty = true;
}

s_bounds_16u Canvas::ClampBoundsToParent(s_bounds_16u bounds, s_bounds_16u parentBounds) {
    s_bounds_16u result = bounds;
    
    // Clamp to parent window boundaries
    if (result.x < parentBounds.x) {
        result.w -= (parentBounds.x - result.x);
        result.x = parentBounds.x;
    }
    if (result.y < parentBounds.y) {
        result.h -= (parentBounds.y - result.y);
        result.y = parentBounds.y;
    }
    if (result.x + result.w > parentBounds.x + parentBounds.w) {
        result.w = (parentBounds.x + parentBounds.w) - result.x;
    }
    if (result.y + result.h > parentBounds.y + parentBounds.h) {
        result.h = (parentBounds.y + parentBounds.h) - result.y;
    }
    
    return result;
}

////////////
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

    // ✅ FIXED: Calculate logicalW and logicalH using the same formula as WinDraw()
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

int safe_parse_int(std::string_view str, int default_val = 0) {
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

uint16_t safe_parse_color(std::string_view str, uint16_t default_val = 0xFFFF) {
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

stdpsram::Vector<TextChunk> Window::tokenize(const stdpsram::String& input) {
    stdpsram::Vector<TextChunk> chunks;
    if (input.empty()) {
        return chunks;
    }

    std::string_view s(input.c_str(), input.length());  // zero-copy view
    stdpsram::String text_buffer;
    text_buffer.reserve((input.length() + 1) / 2 + 32);

    auto flush = [&]() {
        if (!text_buffer.empty()) {
            chunks.emplace_back(TextChunk(std::move(text_buffer)));
            text_buffer.clear();   // note, moved-from string is now empty again
        }
    };

    size_t i = 0;
    while (i < s.length()) {
        // Fast path: normal character
        if (s[i] != '<' || i + 1 >= s.length() || s[i + 1] != '|') {
            text_buffer += s[i];
            ++i;
            continue;
        }

        // Found potential tag: "<|"
        flush();

        size_t end = s.find("|>", i + 2);
        if (end == std::string_view::npos) {
            // Unclosed tag → treat rest as text
            text_buffer = s.substr(i);
            break;
        }

        std::string_view inside = s.substr(i + 2, end - i - 2);
        i = end + 2;

        if (inside.empty()) continue;

        // ───── Short toggle tags ─────
        if (inside.length() <= 2) {
            bool is_off = (inside.length() == 2 && inside[0] == '/');
            char first = is_off ? inside[1] : inside[0];

            switch (first) {
                case 'n':
                    if (!is_off) chunks.emplace_back(TagType::LineBreak);
                    break;

                case 'u':
                    chunks.emplace_back(is_off ? TagType::UnderlineOff : TagType::UnderlineToggle);
                    break;

                case 's':
                    chunks.emplace_back(is_off ? TagType::StrikethroughOff : TagType::StrikethroughToggle);
                    break;

                case 'b':
                    chunks.emplace_back(is_off ? TagType::BoldOff : TagType::BoldToggle);
                    break;

                case 'i':
                    chunks.emplace_back(is_off ? TagType::ItalicOff : TagType::ItalicToggle);
                    break;

                default:
                    // Unknown short tag → treat as literal text
                    text_buffer.append(inside.data(), inside.size());
                    break;
            }
            continue;
        }

        // ───── Value tags ─────
        if (inside.starts_with("color=")) {
            auto val = inside.substr(6);
            uint16_t col = safe_parse_color(val);
            chunks.emplace_back(TagType::ColorChange, ColorTag{col});
        }
        else if (inside.starts_with("size=")) {
            auto val = inside.substr(5);
            int sz = safe_parse_int(val, 1);
            if (sz >= 1 && sz <= 255) {
                chunks.emplace_back(TagType::SizeChange, SizeTag{static_cast<uint8_t>(sz)});
            }
        }
        else if (inside.starts_with("pos=")) {
            size_t comma = inside.find(',', 4);
            if (comma != std::string_view::npos) {
                auto x_str = inside.substr(4, comma - 4);
                auto y_str = inside.substr(comma + 1);
                int16_t x = safe_parse_int(x_str);
                int16_t y = safe_parse_int(y_str);
                chunks.emplace_back(TagType::PosChange, PosTag{x, y});
            }
        }
        else if (inside.starts_with("hl=")) {
            auto val = inside.substr(3);
            uint16_t col = safe_parse_color(val, 0xFFFF);
            chunks.emplace_back(TagType::HighlightChange, HighlighterTag{col, true});
        }
        else {
            // Unknown tag → treat as literal text
            text_buffer.append(inside.data(), inside.size());
        }
    }

    flush();
    return chunks;
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
    

void Window::WinDraw() {
	
	

    
    if (!IsWindowShown) return;
    
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

    ESP_LOGI(TAG, "WinDraw rot=%d | logical(%dx%d) @ (%d,%d) → phys(%d,%d %dx%d)",rot, rawW, rawH, wi_sizing.Xpos, wi_sizing.Ypos, physX, physY, physW, physH);

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

    // === 4. TEXT & CONTENT (unchanged from your working version) ===
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
            const stdpsram::String* pTxt = std::get_if<stdpsram::String>(&chunk.content);
            if (!pTxt || pTxt->empty()) break;
            const auto& txt = *pTxt;

            int rx, ry;
            rotPointLocal(curLX, curLY, rawW, rawH, rot, rx, ry);
            int sx = physX + rx;
            int sy = physY + ry;

            fb_draw_ptext(text_rot_flag,
                          sx, sy,
                          txt,
                          Tstate.color,
                          Tstate.size,
                          12,
                          Tstate.highlight_bg,
                          win_internal_color_background,
                          100,
                          w_font_info);

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
            curLX = p.x;
            curLY = p.y;
            break;
        }
        case TagType::ColorChange:
            Tstate.color = std::get<ColorTag>(chunk.content).value;
            break;
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
        case TagType::UnderlineToggle:    Tstate.underline = true; break;
        case TagType::UnderlineOff:       Tstate.underline = false; break;
        case TagType::StrikethroughToggle:Tstate.strikethrough = true; break;
        case TagType::StrikethroughOff:   Tstate.strikethrough = false; break;
        case TagType::BoldToggle:         Tstate.bold = true; break;
        case TagType::BoldOff:            Tstate.bold = false; break;
        case TagType::ItalicToggle:       Tstate.italic = true; break;
        case TagType::ItalicOff:          Tstate.italic = false; break;
        default: break;
        }
    }

    // === 5. HIGHLIGHT DASHED BORDER (ONLY if window is highlighted) ===
    if (window_highlighted) {
        // isfilled = false → only the dashed outline, not a filled rectangle
        // Adjust thickness, colors and segment_len to your liking
        fb_rect_border(false, 2, physX, physY, physW, physH,
               Currentcfg.BorderColor,
               ((OtherTick & 1) ? 0x0000 : 0xFFFF),
               //what's wild is when in the cpp cert exam i never thought i'd use a?b:c in the real world, and here i am. wild
               8);          // segment_len (dash length)
               //ESP_LOGI(TAG, "highlighted this window!");
    }else {
//i could put more args here if needed
//ESP_LOGI(TAG, "no highlight today");

}

    // === FINISH ===
    currentPhysX = physX;
    currentPhysY = physY;
    OtherTick = !OtherTick;//flipflop
    ESP_LOGI(TAG, "highlight=%d OtherTick=%d dirty=%d", 
         window_highlighted, OtherTick, dirty);
    dirty = false;
    lastUpdateTime = esp_timer_get_time();
}

//guess who found out she needed to do this a lot after making the window system and working on other drivers
//i swear to god bruh
void Window::SetText(const char* newText){
    if (!newText) {
        content.clear();
        isTokenized = false;
        dirty = true;
        return;
    }

    // Direct assignment = copy + null termination handled correctly
    content = stdpsram::String(newText);

    isTokenized = false;
    dirty = true;
}

void Window::SetText(std::string_view text)
{
    content = stdpsram::String(text.data(), text.size());
    isTokenized = false;
    dirty = true;
}

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
    ESP_LOGI(TAG, "Window registered at pos(%d,%d) size(%dx%d), total: %d", 
             window->wi_sizing.Xpos, window->wi_sizing.Ypos,
             window->wi_sizing.Width, window->wi_sizing.Height,
             (int)windows.size());
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

void WindowManager::ClampToArea(s_bounds_16u bounds, bool is_universal) {
    // Implementation depends on what s_bounds_16u is
    // For now, just log that it's called
    ESP_LOGI(TAG, "ClampToArea called (bounds: x=%d, y=%d, w=%d, h=%d, universal=%d)",
             bounds.x, bounds.y, bounds.w, bounds.h, is_universal);
    
    if (is_universal) {
        // Apply to all windows
        for (auto& win : windows) {
            if (win) {
                if (win->wi_sizing.Xpos < bounds.x) 
                    win->wi_sizing.Xpos = bounds.x;
                if (win->wi_sizing.Ypos < bounds.y) 
                    win->wi_sizing.Ypos = bounds.y;
                // etc...
            }
        }
    }
}

void WindowManager::ClampToArea(s_bounds_16u bounds, std::shared_ptr<Window> target) {
    if (!target) return;
    
    ESP_LOGI(TAG, "ClampToArea called for specific window");
    if (target->wi_sizing.Xpos < bounds.x) 
        target->wi_sizing.Xpos = bounds.x;
    if (target->wi_sizing.Ypos < bounds.y) 
        target->wi_sizing.Ypos = bounds.y;
    // Add more clamping logic as needed
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
/*
void WindowManager::SetToolbarActive(bool on) {
    m_toolbarConfig.showToolbar = on;
    tb_dirty = true;
    
    // Force all windows to recalibrate their positions
    for (auto& win : windows) {
        if (win) {
            win->dirty = true;
        }
    }
}
*/
//disabled that because we changed how this fucker operates

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
   // fb_clear(0x0000);  // ✅ Clear screen after repositioning
    if (!m_toolbarConfig.showToolbar) return;
    if (windows_repositioned) return;  // Only reposition once
    
    uint16_t offset = GetToolbarOffset();
    bool moved = false;
    
    for (auto& win : windows) {
        if (!win) continue;
        
        int physX, physY, physW, physH;
        win->get_physical_bounds(physX, physY, physW, physH);
        
        switch (m_toolbarConfig.tb_rot) {
            case 0:  // top toolbar
                if (physY < offset) {
                    // Move window DOWN so its top is below toolbar
                    // Need to convert physical movement back to logical movement
                    int delta = offset - physY;
                    win->wi_sizing.Ypos += delta;
                    win->dirty = true;
                    ESP_LOGI(TAG, "Window '%s' moved down by %d (physical Y %d → %d)", 
                             win->Currentcfg.name, delta, physY, physY + delta);
                }
                break;
                
            case 1:  // left toolbar
                if (physX < offset) {
                    int delta = offset - physX;
                    win->wi_sizing.Xpos += delta;
                    win->dirty = true;
                    ESP_LOGI(TAG, "Window '%s' moved right by %d (physical X %d → %d)", 
                             win->Currentcfg.name, delta, physX, physX + delta);
                }
                break;
                
            case 2:  // bottom toolbar
                {
                    int max_phys_y = v_env.clamped_screen_dim_h - offset - physH;
                    if (physY > max_phys_y) {
                        int delta = physY - max_phys_y;
                        win->wi_sizing.Ypos -= delta;
                        win->dirty = true;
                        ESP_LOGI(TAG, "Window '%s' moved up by %d", 
                                 win->Currentcfg.name, delta);
                    }
                }
                break;
                
            case 3:  // right toolbar
                {
                    int max_phys_x = v_env.clamped_screen_dim_w - offset - physW;
                    if (physX > max_phys_x) {
                        int delta = physX - max_phys_x;
                        win->wi_sizing.Xpos -= delta;
                        win->dirty = true;
                        ESP_LOGI(TAG, "Window '%s' moved left by %d", 
                                 win->Currentcfg.name, delta);
                    }
                }
                break;
        }
        if (moved) {
            windows_repositioned = true;
            tb_dirty = true;
            ESP_LOGI(TAG, "Toolbar repositioning completed");
           // fb_clear(0x0000); //change to background color fixit
        }
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




// Update UpdateAll to handle toolbar
// Update UpdateAll to handle toolbar with cooperative yielding
void WindowManager::UpdateAll(bool force, bool ToolbarUpdate, bool repositionWindows, bool draw_toolbar_ontop) {
    // Only reposition on first run after toolbar change
    if (repositionWindows && !windows_repositioned) {
        RepositionAllWindows();
        tb_dirty = true;  
    }
    
    // Remove dead windows
    for (auto it = windows.begin(); it != windows.end(); ) {
        if (!*it || !(*it)->IsWindowShown) {
            it = windows.erase(it);
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
    
    // Draw toolbar BEHIND windows (if requested)
    if (!draw_toolbar_ontop && ToolbarUpdate && m_toolbarConfig.showToolbar) {
        DrawToolBar();
        vTaskDelay(pdMS_TO_TICKS(1));  // ✅ Yield after toolbar
    }
    
    // Draw all windows - yield between each window
    int window_count = 0;
    for (auto& win : windows) {
        if (!win) continue;
        
        if (force) win->enable_refresh_override = true;
        esp_task_wdt_reset();  // god please stop crashing
        win->WinDraw();
        esp_task_wdt_reset();
        // ✅ Yield every window to let idle task breathe
        window_count++;
        if ((window_count & 0x03) == 0) {  // Every 4 windows
            vTaskDelay(pdMS_TO_TICKS(1));  // 1ms yield
        }
        
        esp_task_wdt_reset();  // Keep watchdog happy too
    }
    
    // Draw toolbar ON TOP (if requested)
    if (draw_toolbar_ontop && ToolbarUpdate && m_toolbarConfig.showToolbar) {
        DrawToolBar();
        vTaskDelay(pdMS_TO_TICKS(1)); //just wait
    }
}