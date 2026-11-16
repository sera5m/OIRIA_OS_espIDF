#include "ui_widget.h"
#include "ui_widgets.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include <stdlib.h>
#include <string.h>

static ui_widget_t* alloc_w(ui_wtype_t type)
{
    ui_widget_t* w = (ui_widget_t*)calloc(1, sizeof(ui_widget_t));
    if (!w) return nullptr;
    w->type    = type;
    w->style   = ui_style_dark();
    w->font    = UI_FONT_8;
    w->visible = true;
    w->maxv    = 1;
    return w;
}

ui_widget_t* ui_w_create(ui_wtype_t type, ui_widget_t* parent)
{
    ui_widget_t* w = alloc_w(type);
    if (w && parent) ui_w_add(parent, w);
    return w;
}

void ui_w_add(ui_widget_t* parent, ui_widget_t* child)
{
    if (!parent || !child) return;
    child->parent = parent;
    child->next   = nullptr;
    if (!parent->first_child) {
        parent->first_child = child;
        return;
    }
    ui_widget_t* t = parent->first_child;
    while (t->next) t = t->next;
    t->next = child;
}

ui_widget_t* ui_w_box(ui_widget_t* parent, int w, int h)
{
    ui_widget_t* b = ui_w_create(UI_W_BOX, parent);
    if (!b) return nullptr;
    b->w = (int16_t)w;
    b->h = (int16_t)h;
    return b;
}

ui_widget_t* ui_w_label(ui_widget_t* parent, const char* text)
{
    ui_widget_t* l = ui_w_create(UI_W_LABEL, parent);
    if (!l) return nullptr;
    l->text = text;
    l->w = 120;
    l->h = 10;
    return l;
}

ui_widget_t* ui_w_bar(ui_widget_t* parent, int value, int max)
{
    ui_widget_t* b = ui_w_create(UI_W_BAR, parent);
    if (!b) return nullptr;
    b->value = value;
    b->maxv  = max > 0 ? max : 1;
    b->w = 100;
    b->h = 12;
    return b;
}

ui_widget_t* ui_w_button(ui_widget_t* parent, const char* text)
{
    ui_widget_t* b = ui_w_create(UI_W_BUTTON, parent);
    if (!b) return nullptr;
    b->text = text;
    b->w = 100;
    b->h = 20;
    return b;
}

ui_widget_t* ui_w_arc(ui_widget_t* parent, int value, int max, int r)
{
    ui_widget_t* a = ui_w_create(UI_W_ARC, parent);
    if (!a) return nullptr;
    a->value = value;
    a->maxv  = max > 0 ? max : 1;
    a->w = a->h = (int16_t)(r * 2);
    return a;
}

ui_widget_t* ui_w_title(ui_widget_t* parent, const char* name)
{
    ui_widget_t* t = ui_w_create(UI_W_TITLE, parent);
    if (!t) return nullptr;
    t->text = name;
    t->w = 120;
    t->h = 14;
    return t;
}

ui_widget_t* ui_w_list(ui_widget_t* parent, int count)
{
    ui_widget_t* l = ui_w_create(UI_W_LIST, parent);
    if (!l) return nullptr;
    l->maxv  = count > 0 ? count : 1;
    l->value = 0;
    l->w = 120;
    l->h = 14 * l->maxv;
    return l;
}

void ui_w_layout_v(ui_widget_t* box)
{
    if (!box) return;
    int y = box->style.pad;
    int inner = box->w - 2 * box->style.pad;
    if (inner < 1) inner = 1;
    for (ui_widget_t* c = box->first_child; c; c = c->next) {
        if (!c->visible) continue;
        c->x = box->style.pad;
        c->y = (int16_t)y;
        if (c->w > inner) c->w = (int16_t)inner;
        y += c->h + box->style.pad;
    }
}

// Filled circle via horizontal spans. fb_circle is still a stub.
static void fill_circle(int cx, int cy, int r, uint16_t color)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
        if (dx > 0)
            fb_line(cx - dx, cy + dy, cx + dx, cy + dy, color);
    }
}

// Rounded rect. Uses style.radius. Hard rect if radius is 0.
static void draw_round_rect(int x, int y, int w, int h,
                            uint16_t fill, uint16_t border, uint8_t radius)
{
    if (radius == 0 || radius * 2 >= w || radius * 2 >= h) {
        fb_rect(true, 1, x, y, w, h, fill, border);
        return;
    }
    // Body
    fb_rect(true, 0, x + radius, y, w - 2 * radius, h, fill, 0);
    fb_rect(true, 0, x, y + radius, w, h - 2 * radius, fill, 0);
    // Corners
    fill_circle(x + radius,         y + radius,         radius, fill);
    fill_circle(x + w - 1 - radius, y + radius,         radius, fill);
    fill_circle(x + radius,         y + h - 1 - radius, radius, fill);
    fill_circle(x + w - 1 - radius, y + h - 1 - radius, radius, fill);
    // Border
    if (border) {
        fb_line(x + radius, y,         x + w - 1 - radius, y,         border);
        fb_line(x + radius, y + h - 1, x + w - 1 - radius, y + h - 1, border);
        fb_line(x,         y + radius, x,         y + h - 1 - radius, border);
        fb_line(x + w - 1, y + radius, x + w - 1, y + h - 1 - radius, border);
    }
}

static void draw_one(ui_widget_t* w, int sx, int sy)
{
    const ui_style_t* s = w->focused ? nullptr : &w->style;
    ui_style_t focus;
    if (w->focused) {
        focus = ui_style_focus(&w->style);
        s = &focus;
    }

    switch (w->type) {
    case UI_W_BOX:
        break;
    case UI_W_LABEL:
        if (w->text)
            ui_label(sx, sy, w->text, s, w->font);
        break;
    case UI_W_BAR:
        ui_bar(sx, sy, w->w, w->h, (int)w->value, (int)w->maxv, s);
        break;
    case UI_W_BUTTON:
        draw_round_rect(sx, sy, w->w, w->h, s->bg, s->border, s->radius);
        if (w->text)
            ui_label(sx + s->pad, sy + (w->h - 8) / 2, w->text, s, w->font);
        break;
    case UI_W_ARC:
        ui_arc(sx + w->w / 2, sy + w->h / 2, w->w / 2,
               (int)w->value, (int)w->maxv, s);
        break;
    case UI_W_SPARK:
        if (w->samples && w->sample_n > 1)
            ui_spark(sx, sy, w->w, w->h, w->samples, w->sample_n, s);
        break;
    case UI_W_TITLE:
        draw_round_rect(sx, sy, w->w, w->h, s->bg, s->border, 0);
        if (w->text)
            ui_label(sx + s->pad, sy + 2, w->text, s, w->font);
        break;
    case UI_W_LIST: {
        int row_h = 14;
        int n = (int)w->maxv;
        for (int i = 0; i < n; i++) {
            bool sel = (i == (int)w->value);
            ui_style_t row = sel ? ui_style_focus(s) : *s;
            int ry = sy + i * row_h;
            draw_round_rect(sx, ry, w->w, row_h - 2, row.bg, row.border, row.radius);
            // Text is supplied by the app via children, or we draw the index.
            // If the list has child labels, draw those; else draw the number.
        }
        // Draw child labels into the rows if present.
        int i = 0;
        for (ui_widget_t* c = w->first_child; c && i < n; c = c->next, i++) {
            if (c->type == UI_W_LABEL && c->text) {
                bool sel = (i == (int)w->value);
                ui_style_t row = sel ? ui_style_focus(s) : *s;
                ui_label(sx + row.pad, sy + i * row_h + 2, c->text, &row, c->font);
            }
        }
        break;
    }
    default:
        break;
    }
}

void ui_w_draw(ui_widget_t* w, int ox, int oy)
{
    if (!w || !w->visible) return;
    int sx = ox + w->x;
    int sy = oy + w->y;
    draw_one(w, sx, sy);
    for (ui_widget_t* c = w->first_child; c; c = c->next)
        ui_w_draw(c, sx, sy);
}

void ui_w_list_set(ui_widget_t* list, int index)
{
    if (!list || list->type != UI_W_LIST) return;
    if (index < 0) index = 0;
    if (index >= list->maxv) index = list->maxv - 1;
    list->value = index;
}

int ui_w_list_get(const ui_widget_t* list)
{
    if (!list || list->type != UI_W_LIST) return 0;
    return (int)list->value;
}

void ui_w_free(ui_widget_t* w)
{
    if (!w) return;
    ui_widget_t* c = w->first_child;
    while (c) {
        ui_widget_t* n = c->next;
        ui_w_free(c);
        c = n;
    }
    free(w);
}
