#include "rs_uart_discovery.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "string.h"

static const char* TAG = "rs_uart_disc";

void rs_uart_discovery_init(rs_peer_table_t* table, uint8_t my_dev_id, rs_peer_role_t my_role) {
    if (!table) return;
    memset(table, 0, sizeof(*table));
    table->my_dev_id = my_dev_id;
    table->my_role = my_role;
}

const rs_peer_t* rs_uart_discovery_find(const rs_peer_table_t* table, uint8_t dev_id) {
    if (!table) return nullptr;
    for (uint8_t i = 0; i < table->count; ++i)
        if (table->peers[i].present && table->peers[i].dev_id == dev_id)
            return &table->peers[i];
    return nullptr;
}

static int add_peer(rs_peer_table_t* table, const rs_hello_payload_t* hello,
                    uint32_t baud, uint8_t uart_port) {
    if (!table || !hello) return 0;
    if (hello->dev_id == table->my_dev_id) return 0; // self

    for (uint8_t i = 0; i < table->count; ++i) {
        if (table->peers[i].dev_id == hello->dev_id) {
            table->peers[i].present = true;
            table->peers[i].role = (rs_peer_role_t)hello->role;
            table->peers[i].baud = baud;
            table->peers[i].uart_port = uart_port;
            table->peers[i].last_seen_ms = (uint32_t)(esp_timer_get_time() / 1000);
            strncpy(table->peers[i].name, hello->name, sizeof(table->peers[i].name) - 1);
            return 0; // updated
        }
    }
    if (table->count >= RS_UART_MAX_PEERS) return 0;

    rs_peer_t* p = &table->peers[table->count++];
    memset(p, 0, sizeof(*p));
    p->present = true;
    p->dev_id = hello->dev_id;
    p->role = (rs_peer_role_t)hello->role;
    p->baud = baud;
    p->uart_port = uart_port;
    p->last_seen_ms = (uint32_t)(esp_timer_get_time() / 1000);
    strncpy(p->name, hello->name, sizeof(p->name) - 1);
    ESP_LOGI(TAG, "peer + id=%u role=%u baud=%lu name=%.12s",
             p->dev_id, (unsigned)p->role, (unsigned long)p->baud, p->name);
    return 1;
}

static void fill_hello(rs_hello_payload_t* h, const rs_peer_table_t* table) {
    memset(h, 0, sizeof(*h));
    h->magic[0] = 'R'; h->magic[1] = 'S'; h->magic[2] = 'H';
    h->version = 1;
    h->dev_id = table->my_dev_id;
    h->role = (uint8_t)table->my_role;
    h->caps = 0x03; // DOM + CMD
    const char* n = (table->my_role == RS_PEER_TYRANT) ? "tyrant" :
                    (table->my_role == RS_PEER_PUPPET) ? "puppet" : "solo";
    strncpy(h->name, n, sizeof(h->name) - 1);
}

static bool install_uart(int uart_num, int tx, int rx, uint32_t baud) {
    uart_driver_delete((uart_port_t)uart_num); // ok if not installed

    uart_config_t cfg = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install((uart_port_t)uart_num, 2048, 2048, 0, nullptr, 0) != ESP_OK)
        return false;
    if (uart_param_config((uart_port_t)uart_num, &cfg) != ESP_OK)
        return false;
    if (uart_set_pin((uart_port_t)uart_num, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
        return false;
    return true;
}

int rs_uart_discovery_probe_port(rs_peer_table_t* table,
                                 int uart_num,
                                 int tx_gpio, int rx_gpio,
                                 uint32_t timeout_ms_per_baud) {
    if (!table) return 0;
    int found = 0;

    rs_hello_payload_t hello{};
    fill_hello(&hello, table);

    for (int bi = 0; bi < RS_UART_PROBE_BAUD_COUNT; ++bi) {
        uint32_t baud = RS_UART_PROBE_BAUDS[bi];
        ESP_LOGI(TAG, "probe UART%d @ %lu", uart_num, (unsigned long)baud);

        if (!install_uart(uart_num, tx_gpio, rx_gpio, baud)) {
            ESP_LOGW(TAG, "uart install failed");
            continue;
        }

        // Pack HELLO into RSDOM packet
        uint8_t pkt[sizeof(RsDomPacketHdr) + sizeof(hello) + 1];
        size_t pn = rsdom_pack(pkt, sizeof pkt, RSDOM_TYPE_HELLO, RSDOM_FLAG_LOGIC,
                               0, (const uint8_t*)&hello, (uint16_t)sizeof(hello));
        if (pn) uart_write_bytes((uart_port_t)uart_num, (const char*)pkt, pn);

        // Wait for a HELLO back (simple read + soft parse)
        uint32_t start = (uint32_t)(esp_timer_get_time() / 1000);
        uint8_t rxbuf[128];
        while ((uint32_t)(esp_timer_get_time() / 1000) - start < timeout_ms_per_baud) {
            int n = uart_read_bytes((uart_port_t)uart_num, rxbuf, sizeof rxbuf,
                                    pdMS_TO_TICKS(20));
            if (n <= 0) continue;

            // Very small scanner: look for RSH magic after SOF pattern in payload
            for (int i = 0; i + (int)sizeof(rs_hello_payload_t) <= n; ++i) {
                auto* cand = (const rs_hello_payload_t*)(rxbuf + i);
                if (cand->magic[0] == 'R' && cand->magic[1] == 'S' && cand->magic[2] == 'H') {
                    found += add_peer(table, cand, baud, (uint8_t)uart_num);
                    // Lock this baud — peer found
                    ESP_LOGI(TAG, "locked UART%d @ %lu", uart_num, (unsigned long)baud);
                    return found;
                }
            }
        }
    }
    return found;
}

void rs_uart_discovery_rx_byte(rs_peer_table_t* table, uint8_t b) {
    (void)table;
    (void)b;
    // Optional: feed into RsDomRx and on HELLO call add_peer
}
