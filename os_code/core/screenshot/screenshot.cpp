#include "screenshot.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

static char path[64];

static void rgb565_to_bgr(uint16_t c, uint8_t* o)
{
    uint8_t r = (c >> 11) & 0x1F;
    uint8_t g = (c >> 5)  & 0x3F;
    uint8_t b =  c        & 0x1F;
    o[0] = (uint8_t)((b << 3) | (b >> 2));
    o[1] = (uint8_t)((g << 2) | (g >> 4));
    o[2] = (uint8_t)((r << 3) | (r >> 2));
}

const char* screenshot_save(void)
{
    if (!framebuffer_front) return nullptr;

    mkdir("/sdcard/shots", 0775);

    int n = 0;
    DIR* d = opendir("/sdcard/shots");
    if (d) {
        struct dirent* e;
        while ((e = readdir(d))) {
            int v = 0;
            if (sscanf(e->d_name, "shot_%d.bmp", &v) == 1 && v >= n) n = v + 1;
        }
        closedir(d);
    }
    snprintf(path, sizeof(path), "/sdcard/shots/shot_%d.bmp", n);

    const int w = SCREEN_W, h = SCREEN_H;
    const int row = (w * 3 + 3) & ~3;          /* 4-byte aligned */
    const uint32_t pix = (uint32_t)row * h;
    const uint32_t off = 54;
    const uint32_t fsz = off + pix;

    FILE* f = fopen(path, "wb");
    if (!f) return nullptr;

    uint8_t hdr[54] = {};
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2,  &fsz, 4);
    uint32_t o = off; memcpy(hdr + 10, &o, 4);
    uint32_t hs = 40; memcpy(hdr + 14, &hs, 4);
    int32_t ww = w, hh = h; memcpy(hdr + 18, &ww, 4); memcpy(hdr + 22, &hh, 4);
    uint16_t pl = 1, bpp = 24; memcpy(hdr + 26, &pl, 2); memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &pix, 4);
    fwrite(hdr, 1, 54, f);

    uint8_t* line = (uint8_t*)malloc(row);
    if (!line) { fclose(f); return nullptr; }
    memset(line, 0, row);

    /* BMP is bottom-up. */
    for (int y = h - 1; y >= 0; --y) {
        const uint16_t* src = framebuffer_front + y * w;
        for (int x = 0; x < w; ++x) rgb565_to_bgr(src[x], line + x * 3);
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
    return path;
}
