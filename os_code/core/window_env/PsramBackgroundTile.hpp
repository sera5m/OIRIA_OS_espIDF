#ifndef OS_CODE_CORE_WINDOW_ENV_PSRAM_BACKGROUND_TILE_HPP_
#define OS_CODE_CORE_WINDOW_ENV_PSRAM_BACKGROUND_TILE_HPP_

#include <stdint.h>
#include <memory>
#include "esp_heap_caps.h"
#include "esp_log.h"

// ---------------------------------------------------------------------------
// Background fill patterns
// ---------------------------------------------------------------------------

enum class BgFillType : uint8_t {
    Solid,
    GradientVertical,
    GradientHorizontal,
    Checkerboard,
    Noise,
    Diagonal_lines,
    Transparent,
    triangles,
    waves,
    dots,
    count
};

struct HasDelta {
    bool dx;
    bool dy;
};

// true = pattern repeats on that axis and can be tiled by reference
constexpr HasDelta fillTypeRules[] = {
    /* Solid */              {true,  true },
    /* GradientVertical */   {true,  false},
    /* GradientHorizontal */ {false, true },
    /* Checkerboard */       {true,  true },
    /* Noise */              {false, false},
    /* Diagonal_lines */     {true,  true },
    /* Transparent */        {false, false},
    /* Waves */              {false, true },
    /* dots */               {false, false}
};

struct p_bgTile_cfg {
    uint8_t    win_rotation = 1;
    BgFillType fill_type    = BgFillType::Solid;
    uint16_t   tileSize_x   = 32;
    uint16_t   tileSize_y   = 32;
};

// ---------------------------------------------------------------------------
// Free blit helpers (usable for tiles, icons, bitmaps, etc.)
// ---------------------------------------------------------------------------

void blit_tile(
    uint16_t targetX, uint16_t targetY,
    uint16_t* framebuffer,
    uint16_t* tileBuffer,
    uint16_t tileW, uint16_t tileH);

void blit_tile_clipped(
    uint16_t targetX, uint16_t targetY,
    uint16_t clipX,   uint16_t clipY,
    uint16_t clipW,   uint16_t clipH,
    uint16_t* framebuffer,
    uint16_t* tileBuffer,
    uint16_t tileW,   uint16_t tileH);

// ---------------------------------------------------------------------------
// PSRAM-backed repeating background tile
// ---------------------------------------------------------------------------

class PsramBackgroundTile : public std::enable_shared_from_this<PsramBackgroundTile> {
public:
    bool       allocated          = false;
    uint16_t*  pseudoframebuffer  = nullptr;
    p_bgTile_cfg pbt_cfg;
    uint16_t   primaryColor       = 0xFFFF;
    uint16_t   secondaryColor     = 0x0000;

    explicit PsramBackgroundTile(uint16_t tileSizeX = 32, uint16_t tileSizeY = 32);

    void generate_pattern(BgFillType type, uint16_t primary, uint16_t secondary);

    ~PsramBackgroundTile();
};

#endif // OS_CODE_CORE_WINDOW_ENV_PSRAM_BACKGROUND_TILE_HPP_
