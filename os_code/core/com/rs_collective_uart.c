#include "rs_collective_uart.h"
#include "boot_role.hpp"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "rs_coll_uart";
static RS_UartConf s_conf;
static bool s_ready;

static void fill_defaults(RS_UartConf* c) {
    memset(c, 0, sizeof(*c));
    c->baud_rate = RS_COLL_BAUD;
    c->data_bits = 8;
    c->stop_bits = 1;
    c->parity = 0;
    c->is_dma = false;
    c->rx_buffer_size = 2048;
    c->tx_buffer_size = 2048;
    boot_role_t role = boot_role_resolve();
    c->is_puppet = (role == BOOT_ROLE_PUPPET);
    c->my_dev_id = (uint8_t)role;
    c->puppeteer_id = 1;
    c->gov_mode = c->is_puppet ? PUPPETEER_TYRANT : PUPPETEER_PEER;
    c->topo_mode = PUPPET_PUPPY;
    c->hold_state = LINE_READY;
    c->max_allowable_wait_ms = 200;
    c->current_prio = PRIORITY_STANDARD;
    c->uart_num = RS_COLL_UART_NUM;
    c->tx_gpio = RS_COLL_TX_GPIO;
    c->rx_gpio = RS_COLL_RX_GPIO;
}

void rs_coll_uart_init(const RS_UartConf* conf) {
    if (s_ready) return;
    if (conf) s_conf = *conf;
    else fill_defaults(&s_conf);

    uart_config_t uc = {
        .baud_rate = (int)s_conf.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_port_t port = (uart_port_t)s_conf.uart_num;
    esp_err_t e = uart_driver_install(port, s_conf.rx_buffer_size,
                                      s_conf.tx_buffer_size, 0, NULL, 0);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install %s", esp_err_to_name(e));
        return;
    }
    uart_param_config(port, &uc);
    uart_set_pin(port, s_conf.tx_gpio, s_conf.rx_gpio,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    s_ready = true;
    ESP_LOGI(TAG, "collective UART%d TX=%d RX=%d baud=%lu puppet=%d",
             (int)s_conf.uart_num, (int)s_conf.tx_gpio, (int)s_conf.rx_gpio,
             (unsigned long)s_conf.baud_rate, (int)s_conf.is_puppet);
}

bool rs_coll_uart_ready(void) { return s_ready; }

RS_UartConf* rs_coll_uart_conf(void) { return &s_conf; }

int rs_coll_uart_write(const void* data, size_t n) {
    if (!s_ready || !data || !n) return -1;
    int w = uart_write_bytes((uart_port_t)s_conf.uart_num, data, n);
    return w;
}

int rs_coll_uart_read(void* data, size_t n, uint32_t timeout_ms) {
    if (!s_ready || !data || !n) return -1;
    return uart_read_bytes((uart_port_t)s_conf.uart_num, data, n,
                           pdMS_TO_TICKS(timeout_ms ? timeout_ms : 1));
}
