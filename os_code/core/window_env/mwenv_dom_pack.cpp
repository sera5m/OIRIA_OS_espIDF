// Optional C++ pack helpers – compile only on the logic ESP (ESP1).
// Define MWDOM_HAVE_MWENV=1 and link against MWenv / AnimWorld.

#include "mwenv_dom.hpp"

#if defined(MWDOM_HAVE_MWENV) && MWDOM_HAVE_MWENV

#include "MWenv.hpp"
#include "Canvas.hpp"
#include "AnimWorld.hpp"

int mwdom_pack_window(DomWriter* w, uint8_t index, const Window& win,
                      const AnimWorld* world, const AwCamera* cam) {
    if (!w) return -1;

    DomWindowHeader h{};
    h.index = index;
    h.flags = 0;
    if (win.Currentcfg.borderless) h.flags |= MWDOM_WIN_BORDERLESS;
    if (win.IsWindowShown)         h.flags |= MWDOM_WIN_SHOWN;
    h.rotation       = win.wi_sizing.rotation;
    h.text_size_mult = (uint8_t)win.win_internal_textsize_mult;
    h.x = (int16_t)win.wi_sizing.Xpos;
    h.y = (int16_t)win.wi_sizing.Ypos;
    h.w = win.wi_sizing.Width;
    h.h = win.wi_sizing.Height;
    h.bg_color     = win.win_internal_color_background;
    h.border_color = win.win_internal_color_border;
    h.text_color   = win.win_internal_color_text;
    h.bg_fill_type = (uint8_t)win.win_backgroundpattern;
    h.layer        = (uint8_t)win.Initialcfg.Layer;

    if (!win.content.empty()) h.flags |= MWDOM_WIN_HAS_TEXT;
    if (win.GetCanvas())      h.flags |= MWDOM_WIN_HAS_CANVAS;

    if (mwdom_writer_window(w, &h)) return -1;

    if (h.flags & MWDOM_WIN_HAS_TEXT) {
        const char* t = win.content.c_str();
        uint16_t len = (uint16_t)win.content.size();
        if (mwdom_writer_window_text(w, index, t, len)) return -1;
    }

    // Canvas static shapes
    if (auto canvas = win.GetCanvas()) {
        if (auto* sb = canvas->GetShapeBuffer()) {
            for (uint16_t i = 0; i < sb->count; ++i) {
                const fb_shape_t& sh = sb->shapes[i];
                DomShape ds{};
                ds.win_index = index;
                ds.type  = sh.type;
                ds.layer = sh.layer;
                ds.shown = sh.shown ? 1 : 0;
                ds.x = (int16_t)sh.bounds.x;
                ds.y = (int16_t)sh.bounds.y;
                ds.w = sh.bounds.w;
                ds.h = sh.bounds.h;
                ds.color = sh.color;
                if (mwdom_writer_shape(w, &ds)) return -1;
            }
        }

        // AnimWorld solved objects → pixel AABB in canvas space
        const AnimWorld* aw = world ? world : canvas->GetWorld();
        AwCamera use_cam = cam ? *cam : canvas->GetWorldCamera();
        if (aw) {
            AwObject* tmp[AW_MAX_OBJECTS];
            // gather_alive is non-const; use const API via for_each_sorted
            aw->for_each_sorted([&](const AwObject& o) {
                if (!o.visible || !o.alive) return;
                DomAwObject d{};
                d.win_index = index;
                d.flags = 0;
                if (o.filled) d.flags |= 1;
                if (o.text[0]) d.flags |= 2;

                int x0, y0, x1, y1;
                if (o.shape.valid) {
                    AnimWorld::world_to_pixel(use_cam, o.world_aabb_min_x, o.world_aabb_min_y, x0, y0);
                    AnimWorld::world_to_pixel(use_cam, o.world_aabb_max_x, o.world_aabb_max_y, x1, y1);
                } else {
                    AnimWorld::world_to_pixel(use_cam, o.pos.x, o.pos.y, x0, y0);
                    x1 = x0; y1 = y0;
                }
                if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
                if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
                d.px = (int16_t)x0;
                d.py = (int16_t)y0;
                d.pw = (uint16_t)((x1 - x0) > 0 ? (x1 - x0) : 1);
                d.ph = (uint16_t)((y1 - y0) > 0 ? (y1 - y0) : 1);
                d.color = o.color;
                d.text_color = o.text_color ? o.text_color : o.color;
                d.text_size = o.text_size ? o.text_size : 1;
                d.z_layer = (uint8_t)o.z_layer;
                if (o.text[0]) {
                    strncpy(d.text, o.text, sizeof(d.text) - 1);
                    d.text[sizeof(d.text) - 1] = '\0';
                }
                mwdom_writer_aw_object(w, &d);
            });
        }
    }

    return w->error ? -1 : 0;
}

#endif // MWDOM_HAVE_MWENV
