#include "PsramBackgroundTile.hpp"

#include <math.h>
#include <stdlib.h>
#include <algorithm>

#include "hardware/drivers/abstraction_layers/al_scr.h" // SCREEN_W, SCREEN_H

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

PsramBackgroundTile::PsramBackgroundTile(uint16_t tileSizeX, uint16_t tileSizeY) {
    if (allocated) return;

    pbt_cfg.tileSize_x = tileSizeX;
    pbt_cfg.tileSize_y = tileSizeY;

    size_t sz = (size_t)tileSizeX * tileSizeY * sizeof(uint16_t);
    ESP_LOGI("PsramBG", "Allocating %u×%u tile (%u bytes)", tileSizeX, tileSizeY, (unsigned)sz);

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

// ---------------------------------------------------------------------------
// Pattern generation (runs once into PSRAM)
// ---------------------------------------------------------------------------

void PsramBackgroundTile::generate_pattern(BgFillType type, uint16_t primary, uint16_t secondary) {
    if (!allocated || !pseudoframebuffer) return;

    primaryColor   = primary;
    secondaryColor = secondary;
    uint16_t* bmp  = pseudoframebuffer;
    const uint16_t SX = pbt_cfg.tileSize_x;
    const uint16_t SY = pbt_cfg.tileSize_y;

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
            // Leave untouched
            break;

        case BgFillType::waves: // WARNING: not especially efficient
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
                    int val = (x + y) % 16;
                    bmp[y * SX + x] = (val < 8) ? primary : secondary;
                }
            break;

        case BgFillType::dots:
            for (uint16_t y = 0; y < SY; ++y)
                for (uint16_t x = 0; x < SX; ++x) {
                    bool is_dot = ((x % 4 == 0) && (y % 4 == 0));
                    bmp[y * SX + x] = is_dot ? primary : secondary;
                }
            break;

        case BgFillType::count:
            ESP_LOGW("PsramBG", "BgFillType::count should not be used as a pattern");
            [[fallthrough]];

        default:
            ESP_LOGW("PsramBG", "Unknown pattern %d – using Solid", (int)type);
            for (uint16_t i = 0; i < SX * SY; ++i)
                bmp[i] = primary;
            break;
    }
}

// ---------------------------------------------------------------------------
// Blit helpers
// ---------------------------------------------------------------------------

void blit_tile(
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

void blit_tile_clipped(
    uint16_t targetX, uint16_t targetY,
    uint16_t clipX,   uint16_t clipY,
    uint16_t clipW,   uint16_t clipH,
    uint16_t* framebuffer,
    uint16_t* tileBuffer,
    uint16_t tileW,   uint16_t tileH)
{
    int left   = std::max(static_cast<int>(targetX), static_cast<int>(clipX));
    int top    = std::max(static_cast<int>(targetY), static_cast<int>(clipY));
    int right  = std::min(static_cast<int>(targetX + tileW), static_cast<int>(clipX + clipW));
    int bottom = std::min(static_cast<int>(targetY + tileH), static_cast<int>(clipY + clipH));

    if (left >= right || top >= bottom) return;

    uint16_t src_x  = left - targetX;
    uint16_t src_y  = top  - targetY;
    uint16_t copy_w = right - left;
    uint16_t copy_h = bottom - top;

    for (uint16_t dy = 0; dy < copy_h; ++dy) {
        for (uint16_t dx = 0; dx < copy_w; ++dx) {
            int sx = left + dx;
            int sy = top  + dy;

            if (sx < 0 || sy < 0 || sx >= SCREEN_W || sy >= SCREEN_H) continue;

            uint16_t color = tileBuffer[(src_y + dy) * tileW + (src_x + dx)];
            framebuffer[sy * SCREEN_W + sx] = color;
        }
    }
}
