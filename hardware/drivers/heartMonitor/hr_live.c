#include "hr_live.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "hr_live";

static hr_live_state_t   s_state = HR_LIVE_IDLE;
static hr_live_sample_t  s_ring[HR_LIVE_CAP];
static int               s_n;
static int               s_head;          // next write
static uint32_t          s_session_seq;

const char *hr_live_state_name(hr_live_state_t s)
{
    switch (s) {
        case HR_LIVE_IDLE:    return "idle";
        case HR_LIVE_ARMING:  return "arming";
        case HR_LIVE_RUNNING: return "running";
        case HR_LIVE_PAUSED:  return "paused";
        case HR_LIVE_SAVING:  return "saving";
        case HR_LIVE_ERROR:   return "error";
        default:              return "unknown";
    }
}

esp_err_t hr_live_init(void)
{
    s_state = HR_LIVE_IDLE;
    s_n = 0;
    s_head = 0;
    s_session_seq = 0;
    return ESP_OK;
}

hr_live_state_t hr_live_state(void) { return s_state; }

esp_err_t hr_live_start(void)
{
    if (s_state == HR_LIVE_PAUSED) return hr_live_resume();
    if (s_state != HR_LIVE_IDLE && s_state != HR_LIVE_ERROR)
        return ESP_ERR_INVALID_STATE;
    s_state = HR_LIVE_ARMING;
    s_n = 0;
    s_head = 0;
    s_state = HR_LIVE_RUNNING;
    ESP_LOGI(TAG, "live → running");
    return ESP_OK;
}

esp_err_t hr_live_pause(void)
{
    if (s_state != HR_LIVE_RUNNING) return ESP_ERR_INVALID_STATE;
    s_state = HR_LIVE_PAUSED;
    return ESP_OK;
}

esp_err_t hr_live_resume(void)
{
    if (s_state != HR_LIVE_PAUSED) return ESP_ERR_INVALID_STATE;
    s_state = HR_LIVE_RUNNING;
    return ESP_OK;
}

static esp_err_t save_session(void)
{
    if (s_n == 0) return ESP_OK;
    mkdir("/sdcard/hr", 0775);
    char path[48];
    snprintf(path, sizeof path, "/sdcard/hr/session_%lu.rle", (unsigned long)s_session_seq++);
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGW(TAG, "session save skipped (no %s)", path);
        return ESP_ERR_NOT_FOUND;
    }
    // HRLE v1: magic, version, sample count, then {bpm, count} runs.
    // A jump stays its own run; a flat stretch collapses.
    fwrite("HRLE", 1, 4, f);
    uint8_t ver = 1;
    fwrite(&ver, 1, 1, f);
    uint16_t count = (uint16_t)s_n;
    fwrite(&count, 2, 1, f);

    int start = (s_n == HR_LIVE_CAP) ? s_head : 0;
    uint8_t run_bpm = s_ring[start].bpm;
    uint8_t run_n = 1;
    for (int i = 1; i < s_n; i++) {
        uint8_t b = s_ring[(start + i) % HR_LIVE_CAP].bpm;
        if (b == run_bpm && run_n < 255) {
            run_n++;
        } else {
            fwrite(&run_bpm, 1, 1, f);
            fwrite(&run_n, 1, 1, f);
            run_bpm = b;
            run_n = 1;
        }
    }
    fwrite(&run_bpm, 1, 1, f);
    fwrite(&run_n, 1, 1, f);
    fclose(f);
    ESP_LOGI(TAG, "saved %d samples to %s", s_n, path);
    return ESP_OK;
}

static void clear_ring(void)
{
    s_n = 0;
    s_head = 0;
}

esp_err_t hr_live_stop(void)
{
    if (s_state != HR_LIVE_RUNNING && s_state != HR_LIVE_PAUSED)
        return ESP_ERR_INVALID_STATE;
    s_state = HR_LIVE_SAVING;
    esp_err_t err = save_session();
    clear_ring();
    s_state = (err == ESP_OK || err == ESP_ERR_NOT_FOUND) ? HR_LIVE_IDLE : HR_LIVE_ERROR;
    return err;
}

esp_err_t hr_live_discard(void)
{
    if (s_state != HR_LIVE_RUNNING && s_state != HR_LIVE_PAUSED && s_state != HR_LIVE_ERROR)
        return ESP_ERR_INVALID_STATE;
    clear_ring();
    s_state = HR_LIVE_IDLE;
    ESP_LOGI(TAG, "live discarded");
    return ESP_OK;
}

void hr_live_push(uint8_t bpm, uint8_t spo2, float motion_g, bool rejected)
{
    if (s_state != HR_LIVE_RUNNING || bpm == 0) return;
    hr_live_sample_t s;
    s.bpm = bpm;
    s.spo2 = spo2;
    int mx = (int)(motion_g * 10.0f);
    if (mx < 0) mx = 0;
    if (mx > 255) mx = 255;
    s.motion_x10 = (uint8_t)mx;
    s.flags = rejected ? 1 : 0;
    s_ring[s_head] = s;
    s_head = (s_head + 1) % HR_LIVE_CAP;
    if (s_n < HR_LIVE_CAP) s_n++;
}

int hr_live_count(void) { return s_n; }

int hr_live_copy(hr_live_sample_t *out, int max_n)
{
    if (!out || max_n <= 0 || s_n == 0) return 0;
    int n = s_n < max_n ? s_n : max_n;
    int start = (s_n == HR_LIVE_CAP) ? s_head : 0;
    // if truncated, take the newest max_n
    if (s_n > max_n) start = (s_head - max_n + HR_LIVE_CAP) % HR_LIVE_CAP;
    for (int i = 0; i < n; i++) out[i] = s_ring[(start + i) % HR_LIVE_CAP];
    return n;
}

void hr_live_stats(uint8_t *min_bpm, uint8_t *max_bpm, uint8_t *avg_bpm)
{
    if (s_n == 0) {
        if (min_bpm) *min_bpm = 0;
        if (max_bpm) *max_bpm = 0;
        if (avg_bpm) *avg_bpm = 0;
        return;
    }
    int start = (s_n == HR_LIVE_CAP) ? s_head : 0;
    uint8_t mn = 255, mx = 0;
    uint32_t sum = 0;
    for (int i = 0; i < s_n; i++) {
        uint8_t b = s_ring[(start + i) % HR_LIVE_CAP].bpm;
        if (b < mn) mn = b;
        if (b > mx) mx = b;
        sum += b;
    }
    if (min_bpm) *min_bpm = mn;
    if (max_bpm) *max_bpm = mx;
    if (avg_bpm) *avg_bpm = (uint8_t)(sum / s_n);
}
