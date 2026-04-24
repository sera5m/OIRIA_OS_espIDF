//defines various themes
//this is stupid. we need a global adress for this
// and something like a char literal define version for the ui which uses CHAR "<|0x1234|>" 
#ifndef WENV_BASIC_THEMES_H
#define WENV_BASIC_THEMES_H

#include <stdint.h>
#include <stdio.h>

static inline void u16_to_hex_str(uint16_t v, char out[7])
{
    const char hex[] = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    out[2] = hex[(v >> 12) & 0xF];
    out[3] = hex[(v >> 8)  & 0xF];
    out[4] = hex[(v >> 4)  & 0xF];
    out[5] = hex[v & 0xF];
    out[6] = '\0';
}

typedef struct {
    char primary[7];
    char secondary[7];
    char text[7];
    char secondaryText[7];
    char border[7];
    char transparency[7];
    char ico[7];
    char highlight[7];
} s_theme_str;

typedef struct s_theme {
    uint16_t primary;
    uint16_t secondary;
    uint16_t text;
    uint16_t secondaryText;
    uint16_t border;
    uint16_t transparency;
    uint16_t ico;
    uint16_t highlight;

    s_theme_str str;  // cached strings

#ifdef __cplusplus
    // C++ constructor
    s_theme(
        uint16_t primary,
        uint16_t secondary,
        uint16_t text,
        uint16_t secondaryText,
        uint16_t border,
        uint16_t transparency,
        uint16_t ico,
        uint16_t highlight
    )
        : primary(primary),
          secondary(secondary),
          text(text),
          secondaryText(secondaryText),
          border(border),
          transparency(transparency),
          ico(ico),
          highlight(highlight)
    {
        u16_to_hex_str(primary, str.primary);
        u16_to_hex_str(secondary, str.secondary);
        u16_to_hex_str(text, str.text);
        u16_to_hex_str(secondaryText, str.secondaryText);
        u16_to_hex_str(border, str.border);
        u16_to_hex_str(transparency, str.transparency);
        u16_to_hex_str(ico, str.ico);
        u16_to_hex_str(highlight, str.highlight);
    }

    s_theme() = default;
#endif

} s_theme;

static inline s_theme theme_init(
    uint16_t primary,
    uint16_t secondary,
    uint16_t text,
    uint16_t secondaryText,
    uint16_t border,
    uint16_t transparency,
    uint16_t ico,
    uint16_t highlight
) {
    s_theme t;
    t.primary = primary;
    t.secondary = secondary;
    t.text = text;
    t.secondaryText = secondaryText;
    t.border = border;
    t.transparency = transparency;
    t.ico = ico;
    t.highlight = highlight;
    
    u16_to_hex_str(primary, t.str.primary);
    u16_to_hex_str(secondary, t.str.secondary);
    u16_to_hex_str(text, t.str.text);
    u16_to_hex_str(secondaryText, t.str.secondaryText);
    u16_to_hex_str(border, t.str.border);
    u16_to_hex_str(transparency, t.str.transparency);
    u16_to_hex_str(ico, t.str.ico);
    u16_to_hex_str(highlight, t.str.highlight);
    
    return t;
}

#endif // WENV_BASIC_THEMES_H