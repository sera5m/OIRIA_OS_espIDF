#ifndef RS_COLLECTIVE_UART_H
#define RS_COLLECTIVE_UART_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Governance model defining how decisions, handshakes, and tasks are negotiated.
 */
typedef enum {
    PUPPETEER_TYRANT,      ///< Absolute command. Worker obeys blindly.
    PUPPETEER_PEER,        ///< Co-equal negotiation.
    PUPPETEER_DEMOCRACY,   ///< Consensus-driven.
    PUPPETEER_AUTONOMOUS   ///< Worker picks up queue work without constant oversight.
} PuppeteerType;

typedef enum {
    PUPPET_PARALLEL,
    PUPPET_SERIES,
    PUPPET_MESH,
    PUPPET_PUPPY,
    PUPPET_BROADCAST
} PuppetType;

typedef enum {
    LINE_READY,
    LINE_HOLD_SHORT,
    LINE_HOLD_LONG,
    LINE_BLOCKED_CRITICAL
} RS_HoldState;

typedef enum {
    PRIORITY_BULK_DATA,
    PRIORITY_STANDARD,
    PRIORITY_REALTIME,
    PRIORITY_EMERGENCY
} RS_PriorityLevel;

typedef struct {
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t stop_bits;
    uint8_t parity;
    bool is_dma;
    uint16_t rx_buffer_size;
    uint16_t tx_buffer_size;
    bool is_puppet;
    uint8_t my_dev_id;
    uint8_t puppeteer_id;
    PuppeteerType gov_mode;
    PuppetType topo_mode;
    volatile uint8_t cpu_load;
    uint32_t dropped_packets;
    volatile RS_HoldState hold_state;
    uint32_t hold_timestamp_ms;
    uint32_t max_allowable_wait_ms;
    RS_PriorityLevel current_prio;
    uint8_t uart_num;   /* UART_NUM_1 */
    int8_t  tx_gpio;    /* default 7 — avoid I2C SCL on 8 */
    int8_t  rx_gpio;    /* default 18 */
} RS_UartConf;

/* Default inter-chip link: UART1 @ 921600, GPIO7 TX / GPIO18 RX.
 * GPIO8 is I2C SCL on this board — do not reuse it. */
#ifndef RS_COLL_UART_NUM
#define RS_COLL_UART_NUM  1
#endif
#ifndef RS_COLL_TX_GPIO
#define RS_COLL_TX_GPIO   7
#endif
#ifndef RS_COLL_RX_GPIO
#define RS_COLL_RX_GPIO   18
#endif
#ifndef RS_COLL_BAUD
#define RS_COLL_BAUD      921600
#endif

void        rs_coll_uart_init(const RS_UartConf* conf); /* NULL = boot-role defaults */
bool        rs_coll_uart_ready(void);
RS_UartConf* rs_coll_uart_conf(void);
int         rs_coll_uart_write(const void* data, size_t n);
int         rs_coll_uart_read(void* data, size_t n, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
