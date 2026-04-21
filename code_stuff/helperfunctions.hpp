#ifndef HELPERFUNCTIONS_H
#define HELPERFUNCTIONS_H

#include "code_stuff/types.h"
//#include <cstddef>
//bits
#pragma once

void split_u32_to_24(uint32_t input, uint8_t *last8, uint8_t *color);

inline int16_t wrap_value(int16_t value, int16_t min, int16_t max) {
    int16_t range = max - min + 1;
    if (range <= 0) return min;  // Defensive
    while (value < min) value += range;
    while (value > max) value -= range;
    return value;
}

//templates
template<typename T>
inline T CLAMP(const T& val, const T& min, const T& max) {
    return (val < min) ? min : (val > max) ? max : val;
}



inline void ui8To2char(uint8_t v, char* out) {
    static const char digits[] = "0123456789";

    uint8_t tens = (v * 205) >> 11;
    uint8_t ones = v - tens * 10;

    out[0] = digits[tens];
    out[1] = digits[ones];
}
//weird magic number bitchift lut fuckery that i don't understand well


#endif//end HELPERFUNCTIONS_H
