#pragma once
// =============================================================================
// rs_dom_link – UART framing for DomFrame over the collective / puppet link
// =============================================================================
// Builds on RS_UartConf ideas in rs_collective_uart.h without forcing that
// header into every TU.
//
// Wire packet:
//   [0]     SOF0  0xAA
//   [1]     SOF1  0x55
//   [2]     type  RSDOM_TYPE_*
//   [3]     flags
//   [4..5]  seq   uint16 LE
//   [6..7]  len   uint16 LE  (payload bytes)
//   [8..]   payload
//   [8+len] crc8 over type..payload
//
// Payload for RSDOM_TYPE_DOM is a complete DomFrame (header + body).
// =============================================================================

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "mwenv_dom.hpp"

#define RSDOM_SOF0          0xAA
#define RSDOM_SOF1          0x55

#define RSDOM_TYPE_DOM      0x01   // full / delta DomFrame
#define RSDOM_TYPE_ACK      0x02
#define RSDOM_TYPE_HOLD     0x03   // flow control (maps to RS_HoldState)
#define RSDOM_TYPE_HELLO    0x04   // capability / role announce
#define RSDOM_TYPE_PING     0x05

#define RSDOM_FLAG_NEED_ACK 0x01
#define RSDOM_FLAG_PUPPET   0x02   // sender is worker
#define RSDOM_FLAG_LOGIC    0x04   // sender is logic/orchestrator

#pragma pack(push, 1)
typedef struct {
    uint8_t  sof0;
    uint8_t  sof1;
    uint8_t  type;
    uint8_t  flags;
    uint16_t seq;
    uint16_t len;
} RsDomPacketHdr;
#pragma pack(pop)

static inline uint8_t rsdom_crc8(const uint8_t* data, size_t n) {
    uint8_t c = 0x00;
    for (size_t i = 0; i < n; ++i) {
        c ^= data[i];
        for (int b = 0; b < 8; ++b)
            c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    }
    return c;
}

// Pack DomFrame bytes into an RSDOM packet. Returns total packet size or 0.
static inline size_t rsdom_pack(uint8_t* out, size_t out_cap,
                                uint8_t type, uint8_t flags, uint16_t seq,
                                const uint8_t* payload, uint16_t payload_len) {
    const size_t need = sizeof(RsDomPacketHdr) + payload_len + 1;
    if (!out || out_cap < need) return 0;

    RsDomPacketHdr* h = (RsDomPacketHdr*)out;
    h->sof0  = RSDOM_SOF0;
    h->sof1  = RSDOM_SOF1;
    h->type  = type;
    h->flags = flags;
    h->seq   = seq;
    h->len   = payload_len;
    if (payload_len && payload)
        memcpy(out + sizeof(RsDomPacketHdr), payload, payload_len);

    // CRC over type, flags, seq, len, payload
    uint8_t crc = rsdom_crc8(&out[2], 2 + 2 + 2 + payload_len);
    out[sizeof(RsDomPacketHdr) + payload_len] = crc;
    return need;
}

// Incremental RX parser (call with each UART byte).
typedef enum {
    RSDOM_RX_WAIT_SOF0 = 0,
    RSDOM_RX_WAIT_SOF1,
    RSDOM_RX_HDR,
    RSDOM_RX_PAYLOAD,
    RSDOM_RX_CRC,
} RsDomRxState;

typedef struct {
    RsDomRxState state;
    RsDomPacketHdr hdr;
    uint8_t  hdr_i;
    uint8_t* payload;      // caller-owned buffer
    uint16_t payload_cap;
    uint16_t payload_i;
    uint8_t  got_crc;
    int      ready;        // 1 when a full good packet is in payload/hdr
} RsDomRx;

static inline void rsdom_rx_init(RsDomRx* rx, uint8_t* payload_buf, uint16_t payload_cap) {
    memset(rx, 0, sizeof(*rx));
    rx->payload = payload_buf;
    rx->payload_cap = payload_cap;
    rx->state = RSDOM_RX_WAIT_SOF0;
}

// Returns 1 when a packet is complete and CRC-ok (see rx->hdr / rx->payload).
static inline int rsdom_rx_byte(RsDomRx* rx, uint8_t b) {
    if (!rx) return 0;
    rx->ready = 0;

    switch (rx->state) {
    case RSDOM_RX_WAIT_SOF0:
        if (b == RSDOM_SOF0) rx->state = RSDOM_RX_WAIT_SOF1;
        break;
    case RSDOM_RX_WAIT_SOF1:
        if (b == RSDOM_SOF1) {
            rx->state = RSDOM_RX_HDR;
            rx->hdr_i = 0;
            rx->hdr.sof0 = RSDOM_SOF0;
            rx->hdr.sof1 = RSDOM_SOF1;
        } else if (b != RSDOM_SOF0) {
            rx->state = RSDOM_RX_WAIT_SOF0;
        }
        break;
    case RSDOM_RX_HDR: {
        uint8_t* hp = (uint8_t*)&rx->hdr;
        // fill type, flags, seq, len (6 bytes after sof)
        hp[2 + rx->hdr_i] = b;
        rx->hdr_i++;
        if (rx->hdr_i >= 6) {
            if (rx->hdr.len > rx->payload_cap) {
                rx->state = RSDOM_RX_WAIT_SOF0;
                break;
            }
            rx->payload_i = 0;
            rx->state = (rx->hdr.len == 0) ? RSDOM_RX_CRC : RSDOM_RX_PAYLOAD;
        }
        break;
    }
    case RSDOM_RX_PAYLOAD:
        rx->payload[rx->payload_i++] = b;
        if (rx->payload_i >= rx->hdr.len)
            rx->state = RSDOM_RX_CRC;
        break;
    case RSDOM_RX_CRC: {
        // CRC over type..payload
        uint8_t tmp[6];
        tmp[0] = rx->hdr.type;
        tmp[1] = rx->hdr.flags;
        memcpy(tmp + 2, &rx->hdr.seq, 2);
        memcpy(tmp + 4, &rx->hdr.len, 2);
        uint8_t c = rsdom_crc8(tmp, 6);
        if (rx->hdr.len)
            c = rsdom_crc8(rx->payload, rx->hdr.len) ^ c; // not ideal combine – recompute properly:
        // Proper: CRC(type||flags||seq||len||payload)
        {
            uint8_t c2 = 0;
            c2 = rsdom_crc8(tmp, 6);
            // restart – simple approach:
            uint8_t* stream = nullptr;
            // recompute on stack for small frames
            uint8_t stack[6 + 256];
            size_t n = 6 + rx->hdr.len;
            if (n <= sizeof(stack)) {
                memcpy(stack, tmp, 6);
                if (rx->hdr.len) memcpy(stack + 6, rx->payload, rx->hdr.len);
                c2 = rsdom_crc8(stack, n);
            }
            if (c2 == b) {
                rx->ready = 1;
            }
        }
        rx->state = RSDOM_RX_WAIT_SOF0;
        return rx->ready;
    }
    }
    return 0;
}

// Suggested baud for Dom streaming on short wires: 2_000_000
// RS_UartConf.cfg.baud_rate = 2000000; is_dma = true; rx/tx buffers ≥ 2048

// ---------------------------------------------------------------------------
// Lifecycle (implemented in rs_dom_link.cpp) – called from bootloader_final_app
// ---------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

void   rs_dom_link_start_tx(void);   // puppet: begin DOM transmit path
void   rs_dom_link_start_rx(void);   // tyrant: begin DOM receive path
bool   rs_dom_link_tx_active(void);
bool   rs_dom_link_rx_active(void);
size_t rs_dom_link_send_frame(const uint8_t* frame, uint16_t frame_len);

#ifdef __cplusplus
}
#endif
