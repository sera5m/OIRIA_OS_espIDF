#pragma once

#include <cstdint>
#include "class/hid/hid.h"   // TinyUSB HID_KEY_*

// Internal key definitions (16-bit)
#define KEY_A       ((uint16_t)'A')
#define KEY_B       ((uint16_t)'B')
#define KEY_C       ((uint16_t)'C')
#define KEY_D       ((uint16_t)'D')
#define KEY_E       ((uint16_t)'E')
#define KEY_F       ((uint16_t)'F')
#define KEY_G       ((uint16_t)'G')
#define KEY_H       ((uint16_t)'H')
#define KEY_I       ((uint16_t)'I')
#define KEY_J       ((uint16_t)'J')
#define KEY_K       ((uint16_t)'K')
#define KEY_L       ((uint16_t)'L')
#define KEY_M       ((uint16_t)'M')
#define KEY_N       ((uint16_t)'N')
#define KEY_O       ((uint16_t)'O')
#define KEY_P       ((uint16_t)'P')
#define KEY_Q       ((uint16_t)'Q')
#define KEY_R       ((uint16_t)'R')
#define KEY_S       ((uint16_t)'S')
#define KEY_T       ((uint16_t)'T')
#define KEY_U       ((uint16_t)'U')
#define KEY_V       ((uint16_t)'V')
#define KEY_W       ((uint16_t)'W')
#define KEY_X       ((uint16_t)'X')
#define KEY_Y       ((uint16_t)'Y')
#define KEY_Z       ((uint16_t)'Z')

#define KEY_0       ((uint16_t)'0')
#define KEY_1       ((uint16_t)'1')
#define KEY_2       ((uint16_t)'2')
#define KEY_3       ((uint16_t)'3')
#define KEY_4       ((uint16_t)'4')
#define KEY_5       ((uint16_t)'5')
#define KEY_6       ((uint16_t)'6')
#define KEY_7       ((uint16_t)'7')
#define KEY_8       ((uint16_t)'8')
#define KEY_9       ((uint16_t)'9')

// Special keys
#define KEY_UP          0x2191
#define KEY_DOWN        0x2193
#define KEY_LEFT        0x2190
#define KEY_RIGHT       0x2192
#define KEY_ENTER       0x23CE
#define KEY_BACK        0x232B
#define KEY_SPACE       ((uint16_t)' ')
#define KEY_TAB         0x0009
#define KEY_ESC         0x001B
#define KEY_BACKSPACE   0x0008
#define KEY_DELETE      0x007F

// Public API
uint8_t get_hid_keycode(uint16_t internal_key, uint8_t* out_modifiers = nullptr);
const char* get_key_name(uint16_t internal_key);