#pragma once
#include <stdint.h>
//#include <math.h>
#include <stdbool.h>

typedef struct{
    uint16_t x;
    uint16_t y;
} fontcharsize;

extern const fontcharsize font6x8;

typedef struct {
   const uint8_t* fontRef;
   fontcharsize fcs;
    bool useExactColors; //if disabled, uses black and white. if NOT, use normal font coloration. 
    //bool should only be enabled if you need a multicolor font or something wacky
} fontdata;
//very nice collection of the data. 4/13/2026

//
extern const fontdata ft_AVR_classic_6x8;
//end h