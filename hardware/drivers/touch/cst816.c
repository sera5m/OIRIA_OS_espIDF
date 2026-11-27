#include "cst816.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "cst816";

#define REG_GESTURE   0x01
#define REG_FINGERS   0x02
#define REG_XH        0x03
#define REG_CHIPID    0xA7

/* Second point, if the chip packs it. Some variants use 0x09. */
#define REG_P2        0x09

struct cst816_dev {
    i2c_master_dev_handle_t i2c;
    gpio_num_t rst;
    gpio_num_t irq;
};

static esp_err_t rd(cst816_dev_t* d, uint8_t reg, uint8_t* buf, size_t n)
{
    return i2c_master_transmit_receive(d->i2c, &reg, 1, buf, n, 50);
}

esp_err_t cst816_init(const cst816_cfg_t* cfg, cst816_dev_t** out)
{
    ESP_RETURN_ON_FALSE(cfg && out && cfg->bus, ESP_ERR_INVALID_ARG, TAG, "cfg");
    cst816_dev_t* d = calloc(1, sizeof(*d));
    ESP_RETURN_ON_FALSE(d, ESP_ERR_NO_MEM, TAG, "alloc");
    d->rst = cfg->rst;
    d->irq = cfg->irq;

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CST816_I2C_ADDR,
        .scl_speed_hz    = cfg->speed_hz ? cfg->speed_hz : 100000,
    };
    esp_err_t e = i2c_master_bus_add_device(cfg->bus, &dc, &d->i2c);
    if (e != ESP_OK) { free(d); return e; }

    if (cfg->rst != GPIO_NUM_NC) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << cfg->rst,
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&io);
        cst816_reset(d);
    }
    if (cfg->irq != GPIO_NUM_NC) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << cfg->irq,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        gpio_config(&io);
    }
    *out = d;
    ESP_LOGI(TAG, "CST816 on bus, addr 0x%02x", CST816_I2C_ADDR);
    return ESP_OK;
}

esp_err_t cst816_reset(cst816_dev_t* d)
{
    if (!d || d->rst == GPIO_NUM_NC) return ESP_OK;
    gpio_set_level(d->rst, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(d->rst, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

esp_err_t cst816_read_id(cst816_dev_t* d, uint8_t* id)
{
    ESP_RETURN_ON_FALSE(d && id, ESP_ERR_INVALID_ARG, TAG, "id");
    return rd(d, REG_CHIPID, id, 1);
}

static void unpack(const uint8_t* b, cst816_point_t* p)
{
    /* XH: [7:4] reserved/event, [3:0] x high. XL: x low. Same for Y. */
    p->x  = ((uint16_t)(b[0] & 0x0F) << 8) | b[1];
    p->y  = ((uint16_t)(b[2] & 0x0F) << 8) | b[3];
    p->id = (b[0] >> 4) & 0x0F;
}

esp_err_t cst816_read(cst816_dev_t* d, cst816_report_t* out)
{
    ESP_RETURN_ON_FALSE(d && out, ESP_ERR_INVALID_ARG, TAG, "read");
    memset(out, 0, sizeof(*out));

    uint8_t hdr[2] = {0};
    esp_err_t e = rd(d, REG_GESTURE, hdr, 2);
    if (e != ESP_OK) return e;
    out->gesture = (cst816_gesture_t)hdr[0];
    uint8_t n = hdr[1] & 0x0F;
    if (n == 0) return ESP_OK;
    if (n > CST816_MAX_POINTS) n = CST816_MAX_POINTS;

    uint8_t p0[4];
    e = rd(d, REG_XH, p0, 4);
    if (e != ESP_OK) return e;
    unpack(p0, &out->pt[0]);
    out->n = 1;

    if (n >= 2) {
        uint8_t p1[4];
        if (rd(d, REG_P2, p1, 4) == ESP_OK) {
            unpack(p1, &out->pt[1]);
            /* A zero second point means the chip did not actually give one. */
            if (out->pt[1].x || out->pt[1].y) out->n = 2;
        }
    }
    return ESP_OK;
}

bool cst816_irq_asserted(cst816_dev_t* d)
{
    if (!d || d->irq == GPIO_NUM_NC) return true;
    return gpio_get_level(d->irq) == 0;
}

void cst816_deinit(cst816_dev_t* d)
{
    if (!d) return;
    if (d->i2c) i2c_master_bus_rm_device(d->i2c);
    free(d);
}
