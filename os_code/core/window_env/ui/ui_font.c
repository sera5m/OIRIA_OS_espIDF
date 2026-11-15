#include "ui_font.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "font_digits.h"

static const fontdata ft_8x16 = {
    avrclassic_font8x16,
    {8, 16},
    false
};

fontdata ui_font(ui_font_size_t size)
{
    if (size == UI_FONT_DIGITS) return ft_digits_8x8;
    if (size >= 12) return ft_8x16;
    return ft_AVR_classic_6x8;
}

fontdata ui_font_face(ui_font_face_t face)
{
    switch (face) {
        case UI_FACE_AVR16:  return ft_8x16;
        case UI_FACE_DIGITS: return ft_digits_8x8;
        case UI_FACE_AVR6:
        default:             return ft_AVR_classic_6x8;
    }
}
