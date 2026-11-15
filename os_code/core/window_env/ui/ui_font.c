#include "ui_font.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"

// 8x16 is already in the font file, just not wrapped as a fontdata.
static const fontdata ft_8x16 = {
    avrclassic_font8x16,
    {8, 16},
    false
};

fontdata ui_font(ui_font_size_t size)
{
    if (size >= 12) return ft_8x16;
    return ft_AVR_classic_6x8;
}
