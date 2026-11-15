#include "lcd_color.h"

// ST7789 default. Set false for a panel that already matches host order.
bool lcd_byteswap = true;

void lcd_set_byteswap(bool on)
{
    lcd_byteswap = on;
}

void lcd_swap_buffer(uint16_t* buf, size_t n)
{
    if (!buf || !lcd_byteswap) return;
    for (size_t i = 0; i < n; i++) {
        buf[i] = __builtin_bswap16(buf[i]);
    }
}
