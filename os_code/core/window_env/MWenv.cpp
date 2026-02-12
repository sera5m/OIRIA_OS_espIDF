
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
    std::vector<TextChunk> out;
    out.emplace_back(s); // treat everything as plain text
    return out;
}

void Window::WinDraw() {
    if (!IsWindowShown) return;

    const int rot = wi_sizing.rotation & 3;
    const int W   = wi_sizing.Width;
    const int H   = wi_sizing.Height;

    const int drawW = (rot % 2) ? H : W;
    const int drawH = (rot % 2) ? W : H;

    // window background / border (screen space box)
    fb_rect(true, 0,
            wi_sizing.Xpos, wi_sizing.Ypos,
            drawW, drawH,
            win_internal_color_background, win_internal_color_border);

    if (!Currentcfg.borderless) {
        fb_rect(false, 1,
                wi_sizing.Xpos, wi_sizing.Ypos,
                drawW, drawH,
                0x0000, win_internal_color_border);
    }

    TextState state{};
    state.color = win_internal_color_text;
    state.size  = win_internal_textsize_mult;

    // INTERNAL cursor space only
int curLX = 2;
int curLY = 2;

auto chunks = tokenize(content);

for (const auto& chunk : chunks) {

    switch (chunk.kind) {

    case TagType::PlainText: {
        const auto& txt = std::get<std::string>(chunk.content);

        int sx, sy;
        LocalToScreen(curLX, curLY, sx, sy);

        fb_draw_text(
            rot*4, //note that rotation is weird for drawtext takes angles in res of 4 per 90 deg(22.5 deg res max), so since the window only has 4 valid rotations, we will simply multiply window rotation (I,II,III,IV) QUADRANT ONLY by 4(90deg)
            sx,
            sy,
            txt.c_str(),
            state.color,
            state.size,
            nullptr,
            avrclassic_font6x8,
            false,
            false,
            win_internal_color_background,
            32,
            font6x8
        );

        // advance in LOCAL space only
        curLX += txt.length() * 6 * state.size;
        break;
    }

    case TagType::LineBreak:
        curLX = 2;
        curLY += 8 * state.size;
        break;

    case TagType::PosChange: {
        auto p = std::get<PosTag>(chunk.content);
        curLX = p.x;
        curLY = p.y;
        break;
    }

    default:
        break;
    }
}


    dirty = false;
    lastUpdateTime = esp_timer_get_time();
}


