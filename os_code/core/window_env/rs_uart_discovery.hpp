#pragma once
// =============================================================================
// rs_uart_discovery – find / claim collaborative UART peers at boot or on demand
// =============================================================================
// Strategy (practical on 2-wire UART, no multi-drop addressing in hardware):
//
//  1) Fixed pin map per board revision (fast path).
//  2) HELLO broadcast at several candidate baud rates.
//  3) Peer replies with HELLO_ACK (role, dev_id, capabilities).
//  4) Tyrant keeps a small peer table; puppets remember puppeteer_id.
//
// This is not USB enumeration — it's an active probe + timeout.
// =============================================================================

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "rs_dom_link.hpp"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RS_UART_MAX_PEERS
#define RS_UART_MAX_PEERS 4
#endif

typedef enum {
    RS_PEER_NONE = 0,
    RS_PEER_TYRANT,
    RS_PEER_PUPPET,
    RS_PEER_UNKNOWN,
} rs_peer_role_t;

typedef struct {
    bool     present;
    uint8_t  dev_id;
    rs_peer_role_t role;
    uint32_t baud;
    uint8_t  uart_port;      // UART_NUM_x
    uint32_t last_seen_ms;
    char     name[16];
} rs_peer_t;

typedef struct {
    rs_peer_t peers[RS_UART_MAX_PEERS];
    uint8_t   count;
    uint8_t   my_dev_id;
    rs_peer_role_t my_role;
} rs_peer_table_t;

// Candidate bauds to try when probing (short timeouts)
static const uint32_t RS_UART_PROBE_BAUDS[] = {
    2000000, 1000000, 921600, 460800, 115200
};
static const int RS_UART_PROBE_BAUD_COUNT =
    (int)(sizeof(RS_UART_PROBE_BAUDS) / sizeof(RS_UART_PROBE_BAUDS[0]));

// HELLO payload (inside RSDOM_TYPE_HELLO)
#pragma pack(push, 1)
typedef struct {
    uint8_t  magic[3];       // 'R','S','H'
    uint8_t  version;        // 1
    uint8_t  dev_id;
    uint8_t  role;           // rs_peer_role_t
    uint16_t caps;           // bit0=DOM, bit1=CMD, bit2=DMA
    char     name[12];
} rs_hello_payload_t;
#pragma pack(pop)

// Init local identity from boot_role
void rs_uart_discovery_init(rs_peer_table_t* table, uint8_t my_dev_id, rs_peer_role_t my_role);

// Blocking probe on one UART port: try bauds, send HELLO, wait ACK up to timeout_ms.
// Returns number of peers newly added (0/1 typically on point-to-point).
int  rs_uart_discovery_probe_port(rs_peer_table_t* table,
                                  int uart_num,
                                  int tx_gpio, int rx_gpio,
                                  uint32_t timeout_ms_per_baud);

// Non-blocking: call from a task; processes one RX byte for HELLO/ACK.
void rs_uart_discovery_rx_byte(rs_peer_table_t* table, uint8_t b);

// Look up
const rs_peer_t* rs_uart_discovery_find(const rs_peer_table_t* table, uint8_t dev_id);

#ifdef __cplusplus
}
#endif
