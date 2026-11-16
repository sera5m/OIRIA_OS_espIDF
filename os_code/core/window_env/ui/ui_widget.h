#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ui_style.h"
#include "ui_font.h"

#ifdef __cplusplus
extern "C" {
#endif

// Widget kinds. The tree is the framework.
// Free functions in ui_widgets.h stay for one-off draws.
typedef enum {
    UI_W_NONE = 0,
    UI_W_BOX,       // container, no paint of its own
    UI_W_LABEL,
    UI_W_BAR,
    UI_W_BUTTON,
    UI_W_ARC,
    UI_W_SPARK,
    UI_W_TITLE,     // name bar at top of a window
    UI_W_LIST       // vertical list; selection is an index
} ui_wtype_t;

typedef struct ui_widget ui_widget_t;

struct ui_widget {
    ui_wtype_t   type;
    int16_t      x, y, w, h;     // relative to parent
    ui_style_t   style;
    ui_font_size_t font;
    const char*  text;           // not owned
    int32_t      value;          // bar/arc/list index
    int32_t      maxv;           // bar/arc max, or list count
    const int16_t* samples;      // spark only, not owned
    int16_t      sample_n;
    bool         focused;
    bool         visible;
    ui_widget_t* parent;
    ui_widget_t* first_child;
    ui_widget_t* next;
};

// Create. Returns null on alloc failure. Parent may be null (root).
ui_widget_t* ui_w_create(ui_wtype_t type, ui_widget_t* parent);

// Convenience constructors. Parent may be null.
ui_widget_t* ui_w_box(ui_widget_t* parent, int w, int h);
ui_widget_t* ui_w_label(ui_widget_t* parent, const char* text);
ui_widget_t* ui_w_bar(ui_widget_t* parent, int value, int max);
ui_widget_t* ui_w_button(ui_widget_t* parent, const char* text);
ui_widget_t* ui_w_arc(ui_widget_t* parent, int value, int max, int r);
ui_widget_t* ui_w_title(ui_widget_t* parent, const char* name);
ui_widget_t* ui_w_list(ui_widget_t* parent, int count);

// Attach an already-created widget under parent.
void ui_w_add(ui_widget_t* parent, ui_widget_t* child);

// Vertical stack. Lays children out with style.pad between them.
// Sets child x/y. Does not change child w/h except to clamp to parent.
void ui_w_layout_v(ui_widget_t* box);

// Draw the tree. ox,oy is the parent's screen origin.
// Clips each widget to its parent bounds.
void ui_w_draw(ui_widget_t* w, int ox, int oy);

// List helpers. index is clamped to [0, maxv).
void ui_w_list_set(ui_widget_t* list, int index);
int  ui_w_list_get(const ui_widget_t* list);

// Free the tree. Safe on null.
void ui_w_free(ui_widget_t* w);

#ifdef __cplusplus
}
#endif
