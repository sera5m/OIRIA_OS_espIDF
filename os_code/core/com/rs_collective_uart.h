#ifndef RS_COLLECTIVE_UART_H
#define RS_COLLECTIVE_UART_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Governance model defining how decisions, handshakes, and tasks are negotiated.
 */
typedef enum {
    PUPPETEER_TYRANT,      ///< Absolute command. Worker obeys blindly, doing exactly what it's told and replying only with execution confirmations.
    PUPPETEER_PEER,        ///< Co-equal negotiation. Chips dynamically swap and balance tasks based on who has free CPU cycles.
    PUPPETEER_DEMOCRACY,   ///< Consensus-driven. Nodes vote on state or execution priority (ideal if expanding to 3+ chips later).
    PUPPETEER_AUTONOMOUS   ///< Decentralized. Worker picks up whatever work is dropped in its queue without constant oversight.
} PuppeteerType;

/**
 * @brief Data topology and pipeline execution pattern (How information physically moves).
 */
typedef enum {
    PUPPET_PARALLEL,   ///< Load-sharing: Large datasets are split in half. Both chips process different chunks simultaneously to save time.
    PUPPET_SERIES,     ///< Assembly pipeline: Chip A handles ingestion/filtering, passes to Chip B for processing, passes to Chip C for output.
    PUPPET_MESH,       ///< Dynamic routing: Many-to-many topology. Data routes through whichever pathway is open and least congested.
    PUPPET_PUPPY,      ///< Remote Procedural Call (RPC): Worker has no local intelligence; it simply triggers hardware registers and GPIO on direct command.
    PUPPET_BROADCAST   ///< One-to-Many: Conductor screams state/telemetry out to all listening worker nodes simultaneously.
} PuppetType;

/**
 * @brief Hardware, identity, and dynamic health tracking configuration for high-speed UART.
 */
typedef enum {
    LINE_READY,             ///< System is clear; proceed with high-speed data transmission.
    LINE_HOLD_SHORT,        ///< Short pause requested (e.g., waiting on a quick sensor read or internal buffer flip).
    LINE_HOLD_LONG,         ///< Long pause requested (e.g., processing a heavy compute block or writing to flash).
    LINE_BLOCKED_CRITICAL   ///< Critical lock: Internal error or emergency state. Stop all transmissions immediately.
} RS_HoldState;

/**
 * @brief Priority level of the currently executing block of work.
 */
typedef enum {
    PRIORITY_BULK_DATA,     ///< Low priority background streaming. Easily paused or dropped without system failure.
    PRIORITY_STANDARD,      ///< Typical command and data packets.
    PRIORITY_REALTIME,      ///< Time-sensitive operations. Can request short holds, but long holds will cause timeouts.
    PRIORITY_EMERGENCY      ///< Un-stoppable system state commands. Bypasses all "HANG ON A SECOND" requests.
} RS_PriorityLevel;

/**
 * @brief Hardware, identity, and dynamic health tracking configuration for high-speed UART.
 */
typedef struct {
    // --- Hardware Serial Configuration ---
    uint32_t baud_rate;             ///< Communications speed (e.g., 2000000 for 2 Mbps DMA throughput)
    uint8_t data_bits;              ///< Character size (Typically 8)
    uint8_t stop_bits;              ///< Framing end bits (Typically 1)
    uint8_t parity;                 ///< Error checking (0 = None, 1 = Odd, 2 = Even)
    
    // --- DMA Settings & Ring Buffers ---
    bool is_dma;                    ///< True if utilizing background hardware GDMA to completely bypass the CPU
    uint16_t rx_buffer_size;        ///< Dedicated RX buffer allocation size (Recommended: 1024 or 2048 bytes for 2 Mbps)
    uint16_t tx_buffer_size;        ///< Dedicated TX buffer allocation size
    
    // --- Architectural Identity & Node Status ---
    bool is_puppet;                 ///< Role flag: true if node is a worker/subordinate, false if it is the primary orchestrator
    uint8_t my_dev_id;               ///< Unique identifier for this specific physical ESP32 node
    uint8_t puppeteer_id;           ///< Hardware address of the controlling node to ignore unauthorized commands
    
    // --- Active Strategy Map ---
    PuppeteerType gov_mode;         ///< Active decision-making framework (Tyrant, Peer, etc.)
    PuppetType topo_mode;           ///< Active pipeline layout (Parallel, Series, etc.)
    
    // --- Overwhelm Prevention & Flow Control Metrics ---
    volatile uint8_t cpu_load;      ///< Dynamically updated CPU utilization (0-100%). Used to throttle incoming data before overloading.
    uint32_t dropped_packets;       ///< Live tracking counter for buffer overruns or corrupted frames at high speed
    
    // --- "Hang On A Second" Backoff Protocol ---
    volatile RS_HoldState hold_state; ///< Current flow-control holding state requested by this chip or the remote chip
    uint32_t hold_timestamp_ms;     ///< Internal millisecond timestamp (`millis()`) tracking exactly when the hold state was entered
    uint32_t max_allowable_wait_ms; ///< The absolute limit the sender is allowed to wait during a hold before giving up and timing out
    RS_PriorityLevel current_prio;  ///< Priority of active stream, dictating if a hold request can be legally honored or ignored
} RS_UartConf;

#endif // RS_COLLABORATION_H
