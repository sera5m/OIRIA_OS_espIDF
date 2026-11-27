#include "touch_get.h"
#include "esp_timer.h"
#include <string.h>

esp_err_t touch_open(i2c_master_bus_handle_t bus,
                     gpio_num_t rst, gpio_num_t irq,
                     cst816_dev_t** out)
{
    cst816_cfg_t c = {
        .bus = bus,
        .rst = rst,
        .irq = irq,
        .speed_hz = 100000,
    };
    return cst816_init(&c, out);
}

static int16_t dz(int16_t v, int16_t zone)
{
    if (v > -zone && v < zone) return 0;
    return v;
}

esp_err_t touch_get_report(cst816_dev_t* dev, const TouchConf* cfg,
                           touch_filtered_t* prev,
                           touch_filtered_t* out)
{
    if (!dev || !cfg || !out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cst816_report_t raw{};
    esp_err_t e = cst816_read(dev, &raw);
    if (e != ESP_OK) return e;
    out->gesture = raw.gesture;

    uint8_t cap = cfg->panel.max_touch_points;
    if (cap > CST816_MAX_POINTS) cap = CST816_MAX_POINTS;
    uint8_t n = raw.n > cap ? cap : raw.n;

    for (uint8_t i = 0; i < n; i++) {
        float sx = raw.pt[i].x * cfg->screen.scale_x;
        float sy = raw.pt[i].y * cfg->screen.scale_y;
        uint16_t x = (uint16_t)sx;
        uint16_t y = (uint16_t)sy;
        if (x >= cfg->screen.screen_w) x = cfg->screen.screen_w - 1;
        if (y >= cfg->screen.screen_h) y = cfg->screen.screen_h - 1;

        int16_t dx = 0, dy = 0;
        if (prev) {
            for (uint8_t j = 0; j < prev->n; j++) {
                if (prev->pt[j].id == raw.pt[i].id) {
                    dx = (int16_t)x - (int16_t)prev->pt[j].screen_x;
                    dy = (int16_t)y - (int16_t)prev->pt[j].screen_y;
                    break;
                }
            }
        }
        dx = dz(dx, cfg->panel.deadzone_x);
        dy = dz(dy, cfg->panel.deadzone_y);

        out->pt[i].id = raw.pt[i].id;
        out->pt[i].screen_x = x;
        out->pt[i].screen_y = y;
        out->pt[i].dx = dx;
        out->pt[i].dy = dy;
        out->pt[i].down = 1;
        out->n++;
    }
    return ESP_OK;
}

int touch_get_events(cst816_dev_t* dev, const TouchConf* cfg,
                     touch_filtered_t* prev,
                     InputEvent* ev, int cap)
{
    if (!ev || cap <= 0) return 0;
    touch_filtered_t cur{};
    if (touch_get_report(dev, cfg, prev, &cur) != ESP_OK) return 0;

    int w = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    for (uint8_t i = 0; i < cur.n && w < cap; i++) {
        InputEvent e{};
        e.key = cur.pt[i].id;
        e.source_device_type = HIDInputDeviceType::Mouse;
        e.timestamp = now;
        e.target = HIDTarget::toTask;
        if (cur.pt[i].dx || cur.pt[i].dy) {
            e.action = KeyAction::PositionDelta;
            e.delta = ((int32_t)cur.pt[i].dx << 16) | (uint16_t)cur.pt[i].dy;
        } else {
            e.action = KeyAction::Press;
        }
        ev[w++] = e;
    }
    /* fingers that were down last time and are gone now */
    if (prev) {
        for (uint8_t j = 0; j < prev->n && w < cap; j++) {
            bool still = false;
            for (uint8_t i = 0; i < cur.n; i++)
                if (cur.pt[i].id == prev->pt[j].id) { still = true; break; }
            if (!still) {
                InputEvent e{};
                e.key = prev->pt[j].id;
                e.action = KeyAction::Release;
                e.source_device_type = HIDInputDeviceType::Mouse;
                e.timestamp = now;
                e.target = HIDTarget::toTask;
                ev[w++] = e;
            }
        }
    }
    if (prev) *prev = cur;
    return w;
}
