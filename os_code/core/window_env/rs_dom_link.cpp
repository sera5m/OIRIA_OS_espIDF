// =============================================================================
// rs_dom_link.cpp – UART DOM link start stubs + eventual TX/RX tasks
// =============================================================================
// Linked from bootloader_final_app(). Safe no-ops until UART pins / driver are
// wired. Replace the bodies with real uart_driver_install + TX/RX tasks.
// =============================================================================

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "rs_dom_link.hpp"
#include "mwenv_dom.hpp"

// Optional: collective conf (may live under os_code/core/com/)
#if __has_include("os_code/core/com/rs_collective_uart.h")
#include "os_code/core/com/rs_collective_uart.h"
#elif __has_include("rs_collective_uart.h")
#include "rs_collective_uart.h"
#endif

static const char* TAG = "rs_dom_link";

// -----------------------------------------------------------------------------
// Shared state (filled when real UART is enabled)
// -----------------------------------------------------------------------------
static bool s_tx_started = false;
static bool s_rx_started = false;
static uint16_t s_tx_seq = 0;

// Scratch for a future TX task (PSRAM-friendly sizes later)
static uint8_t s_frame_buf[MWDOM_MAX_FRAME_BYTES];
static uint8_t s_pkt_buf[sizeof(RsDomPacketHdr) + MWDOM_MAX_FRAME_BYTES + 1];

// -----------------------------------------------------------------------------
// Public API expected by tusb_hid_example_main.cpp
// -----------------------------------------------------------------------------

extern "C" void rs_dom_link_start_tx(void) {
    if (s_tx_started) {
        ESP_LOGW(TAG, "TX already started");
        return;
    }
    s_tx_started = true;
    ESP_LOGI(TAG, "DOM TX start (stub) – wire UART then pack DomFrames here");
    // TODO:
    //   uart_driver_install(UART_NUM_x, rx, tx, ...)
    //   xTaskCreate(rs_dom_tx_task, "dom_tx", 4096, NULL, 5, NULL);
    // tx_task: on notify from app/WM, mwdom_pack_window → rsdom_pack → uart_write
    (void)s_frame_buf;
    (void)s_pkt_buf;
    (void)s_tx_seq;
}

extern "C" void rs_dom_link_start_rx(void) {
    if (s_rx_started) {
        ESP_LOGW(TAG, "RX already started");
        return;
    }
    s_rx_started = true;
    ESP_LOGI(TAG, "DOM RX start (stub) – wire UART then rsdom_rx_byte loop here");
    // TODO:
    //   uart_driver_install(...)
    //   xTaskCreate(rs_dom_rx_task, "dom_rx", 6144, NULL, 5, NULL);
    // rx_task: read bytes → rsdom_rx_byte → on ready, mwdom_reader_* → draw
}

// Optional helpers other TUs can call later
extern "C" bool rs_dom_link_tx_active(void) { return s_tx_started; }
extern "C" bool rs_dom_link_rx_active(void) { return s_rx_started; }

// Emit a pre-built DomFrame (no-op until UART is up). Returns bytes queued / 0.
extern "C" size_t rs_dom_link_send_frame(const uint8_t* frame, uint16_t frame_len) {
    if (!s_tx_started || !frame || !frame_len) return 0;
    size_t pn = rsdom_pack(s_pkt_buf, sizeof s_pkt_buf,
                           RSDOM_TYPE_DOM, RSDOM_FLAG_LOGIC,
                           s_tx_seq++, frame, frame_len);
    if (!pn) {
        ESP_LOGW(TAG, "rsdom_pack failed (len=%u)", (unsigned)frame_len);
        return 0;
    }
    // TODO: uart_write_bytes(UART_NUM_x, s_pkt_buf, pn);
    ESP_LOGD(TAG, "TX frame stub seq=%u pkt=%u", (unsigned)(s_tx_seq - 1), (unsigned)pn);
    return pn;
}
