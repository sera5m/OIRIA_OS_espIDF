// Collective UART: blob (.vul) + start/end streaming; puppet listens and evals.
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>

#include "rs_dom_link.hpp"
#include "os_code/core/com/rs_collective_uart.h"
#include "os_code/core/com/boot_role.hpp"
#include "os_code/core/rs_vm/vm/rs_vm.hpp"
#include "os_code/core/rs_vm/vm/rs_vm_parse.hpp"

static const char* TAG = "rs_dom_link";

static bool s_tx_started, s_rx_started;
static uint16_t s_tx_seq;
static uint8_t s_pkt_buf[sizeof(RsDomPacketHdr) + 1024 + 1];
static uint8_t s_rx_payload[2048];
static char s_stream[4096];
static size_t s_stream_n;
static bool s_in_ascii_seq;
static SemaphoreHandle_t s_eval_mu;

extern "C" void rsvm_install_esp_host(rsvm_t* vm);

extern "C" int rs_coll_eval_src(const char* src, size_t len) {
    if (!src || !len) return -1;
    if (!s_eval_mu) s_eval_mu = xSemaphoreCreateMutex();
    if (s_eval_mu) xSemaphoreTake(s_eval_mu, portMAX_DELAY);
    rsvm_t* vm = (rsvm_t*)malloc(sizeof(rsvm_t));
    int rc = -1;
    if (vm) {
        rsvm_init(vm);
        rsvm_install_esp_host(vm);
        rsvm_parse_err_t err{};
        char* copy = (char*)malloc(len + 1);
        if (copy) {
            memcpy(copy, src, len);
            copy[len] = 0;
            rsvm_status_t st = rsvm_eval(vm, copy, &err);
            if (st != RSVM_OK) {
                ESP_LOGE(TAG, "eval L%d: %s", err.line, err.message);
                rs_dom_link_send(RSDOM_TYPE_VM_ERR,
                                 (const uint8_t*)err.message,
                                 (uint16_t)strnlen(err.message, 200));
                rc = (int)st;
            } else {
                uint8_t ok[4] = {0};
                rs_dom_link_send(RSDOM_TYPE_VM_ACK, ok, 4);
                rc = 0;
            }
            free(copy);
        }
        free(vm);
    }
    if (s_eval_mu) xSemaphoreGive(s_eval_mu);
    return rc;
}

extern "C" size_t rs_dom_link_send(uint8_t type, const uint8_t* payload, uint16_t len) {
    size_t pn = rsdom_pack(s_pkt_buf, sizeof s_pkt_buf, type,
                           rs_coll_uart_conf()->is_puppet ? RSDOM_FLAG_PUPPET : RSDOM_FLAG_LOGIC,
                           s_tx_seq++, payload, len);
    if (!pn) return 0;
    if (!rs_coll_uart_ready()) {
        ESP_LOGD(TAG, "send type=0x%02X pkt=%u (UART down)", type, (unsigned)pn);
        return pn;
    }
    int w = rs_coll_uart_write(s_pkt_buf, pn);
    return w > 0 ? (size_t)w : 0;
}

static void handle_packet(RsDomRx* rx) {
    uint8_t t = rx->hdr.type;
    const char* p = (const char*)rx->payload;
    uint16_t n = rx->hdr.len;
    switch (t) {
    case RSDOM_TYPE_PING:
        rs_dom_link_send(RSDOM_TYPE_ACK, NULL, 0);
        break;
    case RSDOM_TYPE_VM_SRC:
    case RSDOM_TYPE_CMD:
        rs_coll_eval_src(p, n);
        break;
    case RSDOM_TYPE_VM:
        /* bytecode blob — load_bvul if it looks like RSV2 */
        if (n >= 4 && p[0]=='R' && p[1]=='S' && p[2]=='V') {
            rsvm_t* vm = (rsvm_t*)malloc(sizeof(rsvm_t));
            if (vm) {
                rsvm_init(vm);
                rsvm_install_esp_host(vm);
                if (rsvm_load_bvul(vm, (const uint8_t*)p, n) == RSVM_OK)
                    rsvm_run(vm);
                free(vm);
            }
        } else {
            rs_coll_eval_src(p, n);
        }
        break;
    case RSDOM_TYPE_STREAM_BEGIN:
        s_stream_n = 0;
        s_stream[0] = 0;
        break;
    case RSDOM_TYPE_STREAM_CHUNK:
        if (s_stream_n + n + 1 < sizeof s_stream) {
            memcpy(s_stream + s_stream_n, p, n);
            s_stream_n += n;
            s_stream[s_stream_n] = 0;
        }
        break;
    case RSDOM_TYPE_STREAM_END:
        rs_coll_eval_src(s_stream, s_stream_n);
        s_stream_n = 0;
        break;
    case RSDOM_TYPE_CANCEL:
        s_stream_n = 0;
        break;
    default:
        break;
    }
}

static void ascii_line(char* line) {
    /* strip CR */
    size_t L = strlen(line);
    while (L && (line[L-1]=='\r' || line[L-1]=='\n')) line[--L] = 0;
    if (!L) return;

    if (!s_in_ascii_seq) {
        if (strcasecmp(line, "start sequence:") == 0 ||
            strcmp(line, "<<VUL") == 0 ||
            strcasecmp(line, "start sequence") == 0) {
            s_in_ascii_seq = true;
            s_stream_n = 0;
            s_stream[0] = 0;
            return;
        }
        if (strncmp(line, "run ", 4) == 0) {
            /* blob path on SD */
            FILE* f = fopen(line + 4, "rb");
            if (!f) { ESP_LOGW(TAG, "run: no %s", line + 4); return; }
            char buf[4096];
            size_t n = fread(buf, 1, sizeof buf - 1, f);
            fclose(f);
            buf[n] = 0;
            rs_coll_eval_src(buf, n);
            return;
        }
        /* single-line command */
        rs_coll_eval_src(line, L);
        return;
    }
    if (strcasecmp(line, "end sequence.") == 0 ||
        strcasecmp(line, "end sequence") == 0 ||
        strcmp(line, "VUL>>") == 0) {
        s_in_ascii_seq = false;
        rs_coll_eval_src(s_stream, s_stream_n);
        s_stream_n = 0;
        return;
    }
    if (s_stream_n + L + 2 < sizeof s_stream) {
        memcpy(s_stream + s_stream_n, line, L);
        s_stream_n += L;
        s_stream[s_stream_n++] = '\n';
        s_stream[s_stream_n] = 0;
    }
}

static void rx_task(void*) {
    RsDomRx rx;
    rsdom_rx_init(&rx, s_rx_payload, sizeof s_rx_payload);
    char line[256];
    size_t li = 0;
    uint8_t b;
    while (1) {
        int n = rs_coll_uart_read(&b, 1, 50);
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (rsdom_rx_byte(&rx, b)) {
            handle_packet(&rx);
            li = 0;
            continue;
        }
        /* ASCII path when not in binary payload */
        if (rx.state == RSDOM_RX_WAIT_SOF0) {
            if (b == '\n') {
                line[li] = 0;
                ascii_line(line);
                li = 0;
            } else if (b != '\r' && li + 1 < sizeof line) {
                line[li++] = (char)b;
            }
        }
    }
}

extern "C" void rs_dom_link_start_tx(void) {
    if (s_tx_started) return;
    rs_coll_uart_init(NULL);
    s_tx_started = true;
    ESP_LOGI(TAG, "collective TX up");
}

extern "C" void rs_dom_link_start_rx(void) {
    if (s_rx_started) return;
    rs_coll_uart_init(NULL);
    s_rx_started = true;
    xTaskCreate(rx_task, "rs_coll_rx", 8192, NULL, 6, NULL);
    ESP_LOGI(TAG, "collective RX listen (blob + start/end sequence)");
}

extern "C" void rs_dom_link_start_listen(void) {
    rs_dom_link_start_tx();
    rs_dom_link_start_rx();
}

extern "C" bool rs_dom_link_tx_active(void) { return s_tx_started; }
extern "C" bool rs_dom_link_rx_active(void) { return s_rx_started; }

extern "C" size_t rs_dom_link_send_frame(const uint8_t* frame, uint16_t frame_len) {
    if (!frame || !frame_len) return 0;
    return rs_dom_link_send(RSDOM_TYPE_DOM, frame, frame_len);
}
