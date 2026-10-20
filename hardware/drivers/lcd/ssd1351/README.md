# SSD1351 driver (OIRIA style)

Mirrors the ST7789v2 `lcDriver` pattern so the rest of the window environment stays hardware-agnostic.

## Differences accounted for

| Item | ST7789v2 | SSD1351 |
|------|----------|---------|
| Window commands | 0x2A / 0x2B / 0x2C (16-bit) | 0x15 / 0x75 / 0x5C (8-bit) |
| GDDRAM pointer | Can often keep a window open longer | Must re-issue set-column + set-row + RAMWR before every transfer |
| Typical resolution | 240×280 | 128×128 |
| Safe SPI speed | 40–80 MHz | ~10–15 MHz (default 15 MHz) |
| Partial update chunk | 8 rows | 4 rows |
| Color | RGB565 | RGB565 (65K via remap 0x74) |

Public symbols (`framebuffer`, `lcd_fb_display_framebuffer`, `fb_rect`, etc.) keep the same names so `al_scr` and the window environment need only a driver pointer swap.
