#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"

const fontcharsize font6x8 = {6, 8};

const fontdata ft_AVR_classic_6x8 = {
    avrclassic_font6x8,
    {6, 8}, //typing the reference doesn't work for some fucking reason
    false //normal
};
