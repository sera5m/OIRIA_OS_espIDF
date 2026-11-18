#include "ble_link.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>

static const char *TAG = "ble_link";

#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BT_NIMBLE_ENABLED)

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Custom service. Write = commands, Notify = replies + heartbeat.
#define OIRIA_SVC_UUID  0x0A10
#define OIRIA_CMD_UUID  0x0A11
#define OIRIA_NTF_UUID  0x0A12

static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_ntf_handle = 0;
static uint8_t  s_sleep_sec = 30;
static TimerHandle_t s_hb_timer = NULL;

static int gap_event(struct ble_gap_event *event, void *arg);
static void start_adv(void);

static void hb_timer_cb(TimerHandle_t t)
{
    (void)t;
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || s_ntf_handle == 0) return;
    const char *msg = "hb\n";
    struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, 3);
    if (om) ble_gatts_notify_custom(s_conn, s_ntf_handle, om);
}

static void set_conn_params(uint16_t conn, uint8_t sec)
{
    // 1.25 ms units. Prefer a long interval so the radio sleeps.
    uint16_t itvl = (uint16_t)(sec * 800); // sec * 1000 / 1.25
    if (itvl < 24) itvl = 24;              // 30 ms floor
    if (itvl > 3200) itvl = 3200;          // 4 s cap
    struct ble_gap_upd_params p = {
        .itvl_min = itvl,
        .itvl_max = itvl,
        .latency  = 0,
        .supervision_timeout = (uint16_t)(sec * 4 + 20), // 10 ms units, with margin
    };
    ble_gap_update_params(conn, &p);
}

static int cmd_access(uint16_t conn, uint16_t attr,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t buf[128];
    if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;
    ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
    buf[len] = 0;

    // Sleep window packet: 53 4C <sec>
    if (len >= 3 && buf[0] == 0x53 && buf[1] == 0x4C) {
        ble_link_set_sleep_window(buf[2]);
        set_conn_params(conn, buf[2]);
        return 0;
    }

    // Text command. Reuse the remote handler if present.
    extern bool remote_handle_line(const char *line);
    if (buf[0] == '/') remote_handle_line((const char *)buf);
    return 0;
}

static const struct ble_gatt_svc_def svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(OIRIA_SVC_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(OIRIA_CMD_UUID),
                .access_cb = cmd_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_UUID16_DECLARE(OIRIA_NTF_UUID),
                .access_cb = cmd_access,
                .val_handle = &s_ntf_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        },
    },
    { 0 }
};

static void on_sync(void)
{
    start_adv();
}

static void start_adv(void)
{
    struct ble_gap_adv_params ap = {0};
    struct ble_hs_adv_fields f = {0};
    const char *name = "Oiria";
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = (uint8_t *)name;
    f.name_len = strlen(name);
    f.name_is_complete = 1;
    ble_gap_adv_set_fields(&f);
    ap.conn_mode = BLE_GAP_CONN_MODE_UND;
    ap.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                      &ap, gap_event, NULL);
    ESP_LOGI(TAG, "advertising as Oiria");
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            ESP_LOGI(TAG, "central connected");
            set_conn_params(s_conn, s_sleep_sec);
            if (!s_hb_timer) {
                s_hb_timer = xTimerCreate("ble_hb", pdMS_TO_TICKS(s_sleep_sec * 1000),
                                          pdTRUE, NULL, hb_timer_cb);
            }
            xTimerChangePeriod(s_hb_timer, pdMS_TO_TICKS(s_sleep_sec * 1000), 0);
            xTimerStart(s_hb_timer, 0);
        } else {
            start_adv();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        if (s_hb_timer) xTimerStop(s_hb_timer, 0);
        ESP_LOGI(TAG, "central gone, re-advertising");
        start_adv();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_adv();
        break;
    default:
        break;
    }
    return 0;
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_link_start(void)
{
    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Oiria");
    ble_gatts_count_cfg(svcs);
    ble_gatts_add_svcs(svcs);
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "nimble up");
}

bool ble_link_connected(void)
{
    return s_conn != BLE_HS_CONN_HANDLE_NONE;
}

void ble_link_set_sleep_window(uint8_t seconds)
{
    if (seconds < 1) seconds = 1;
    if (seconds > 60) seconds = 60;
    s_sleep_sec = seconds;
    if (s_hb_timer) {
        xTimerChangePeriod(s_hb_timer, pdMS_TO_TICKS(seconds * 1000), 0);
    }
    ESP_LOGI(TAG, "sleep window %us", seconds);
}

#else

void ble_link_start(void)
{
    ESP_LOGW(TAG, "BT not enabled (CONFIG_BT_ENABLED / NIMBLE)");
}
bool ble_link_connected(void) { return false; }
void ble_link_set_sleep_window(uint8_t seconds) { (void)seconds; }

#endif
