#include "keymap.hpp"
#include "esp_log.h"

static const char* TAG = "KeyMap";

// Mapping table - generated with macros for cleanliness
struct KeyMapping {
    uint16_t internal_key;
    uint8_t  hid_code;
    uint8_t  modifiers;
};

#define MAP(internal, hid)          { (internal), (hid), 0 }
#define MAP_SHIFT(internal, hid)    { (internal), (hid), HID_KEY_SHIFT_LEFT }

static const KeyMapping key_mappings[] = {
    // Navigation & Special
    MAP(KEY_UP,        HID_KEY_ARROW_UP),
    MAP(KEY_DOWN,      HID_KEY_ARROW_DOWN),
    MAP(KEY_LEFT,      HID_KEY_ARROW_LEFT),
    MAP(KEY_RIGHT,     HID_KEY_ARROW_RIGHT),
    MAP(KEY_ENTER,     HID_KEY_ENTER),
    MAP(KEY_BACK,      HID_KEY_ESCAPE),
    MAP(KEY_SPACE,     HID_KEY_SPACE),
    MAP(KEY_TAB,       HID_KEY_TAB),
    MAP(KEY_ESC,       HID_KEY_ESCAPE),
    MAP(KEY_BACKSPACE, HID_KEY_BACKSPACE),
    MAP(KEY_DELETE,    HID_KEY_DELETE),

    // Letters A-Z (using ASCII values as internal keys)
    MAP(KEY_A, HID_KEY_A), MAP(KEY_B, HID_KEY_B), MAP(KEY_C, HID_KEY_C),
    MAP(KEY_D, HID_KEY_D), MAP(KEY_E, HID_KEY_E), MAP(KEY_F, HID_KEY_F),
    MAP(KEY_G, HID_KEY_G), MAP(KEY_H, HID_KEY_H), MAP(KEY_I, HID_KEY_I),
    MAP(KEY_J, HID_KEY_J), MAP(KEY_K, HID_KEY_K), MAP(KEY_L, HID_KEY_L),
    MAP(KEY_M, HID_KEY_M), MAP(KEY_N, HID_KEY_N), MAP(KEY_O, HID_KEY_O),
    MAP(KEY_P, HID_KEY_P), MAP(KEY_Q, HID_KEY_Q), MAP(KEY_R, HID_KEY_R),
    MAP(KEY_S, HID_KEY_S), MAP(KEY_T, HID_KEY_T), MAP(KEY_U, HID_KEY_U),
    MAP(KEY_V, HID_KEY_V), MAP(KEY_W, HID_KEY_W), MAP(KEY_X, HID_KEY_X),
    MAP(KEY_Y, HID_KEY_Y), MAP(KEY_Z, HID_KEY_Z),

    // Numbers
    MAP(KEY_0, HID_KEY_0), MAP(KEY_1, HID_KEY_1), MAP(KEY_2, HID_KEY_2),
    MAP(KEY_3, HID_KEY_3), MAP(KEY_4, HID_KEY_4), MAP(KEY_5, HID_KEY_5),
    MAP(KEY_6, HID_KEY_6), MAP(KEY_7, HID_KEY_7), MAP(KEY_8, HID_KEY_8),
    MAP(KEY_9, HID_KEY_9),

    // Add more here (symbols with shift, F-keys, etc.)
    // Example: MAP_SHIFT(0x0021, HID_KEY_1), // '!'

    {0, 0, 0} // terminator
};

uint8_t get_hid_keycode(uint16_t internal_key, uint8_t* out_modifiers)
{
    if (out_modifiers) *out_modifiers = 0;

    for (int i = 0; key_mappings[i].internal_key != 0; ++i) {
        if (key_mappings[i].internal_key == internal_key) {
            if (out_modifiers) *out_modifiers = key_mappings[i].modifiers;
            return key_mappings[i].hid_code;
        }
    }

    ESP_LOGW(TAG, "Unknown internal key: 0x%04X", internal_key);
    return 0;
}

const char* get_key_name(uint16_t key) {
    return "UNKNOWN";   // TODO: expand if needed for debugging
}