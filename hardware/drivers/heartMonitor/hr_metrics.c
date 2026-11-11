#include "hr_metrics.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "hr_metrics";

#define NVS_NS   "hrlog"
#define NVS_KEY  "blk"
#define SD_PATH  "/sdcard/hr/hrlog.bin"

// Per-minute raw window.
#define RAW_N    128
#define RED_N    8          // 128 / 16
#define MINS_PER_BLOCK 5

static uint8_t  s_raw_bpm[RAW_N];
static uint8_t  s_raw_spo2[RAW_N];
static uint8_t  s_raw_n;

static uint8_t  s_red_bpm[MINS_PER_BLOCK][RED_N];
static uint8_t  s_red_spo2[MINS_PER_BLOCK];
static uint8_t  s_mins;                 // 0..5

static uint8_t  s_blob[HR_METRICS_NVS_SIZE];
static uint16_t s_used;                 // bytes occupied after the 4-byte header
static bool     s_sd_ok;
static bool     s_inited;

// Blob layout:
//   uint16 used | uint16 blocks | payload...
// Payload record (RLE of one 5-minute block):
//   uint32 t_min | uint8 n_runs | uint8 spo2_avg | n_runs * {uint8 bpm, uint8 count}

static void blob_load(void)
{
    memset(s_blob, 0, sizeof(s_blob));
    s_used = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_blob);
    if (nvs_get_blob(h, NVS_KEY, s_blob, &len) == ESP_OK && len >= 4) {
        s_used = (uint16_t)(s_blob[0] | (s_blob[1] << 8));
        if (s_used > HR_METRICS_NVS_SIZE - 4) s_used = 0;
    }
    nvs_close(h);
}

static void blob_store(void)
{
    s_blob[0] = (uint8_t)(s_used & 0xFF);
    s_blob[1] = (uint8_t)(s_used >> 8);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY, s_blob, sizeof(s_blob));
    nvs_commit(h);
    nvs_close(h);
}

// Reduce up to 128 samples to 8.
// Each bucket keeps the sample furthest from the previous kept value,
// so a spike survives into the RLE instead of being averaged away.
// A flat minute still collapses to one run later.
static void reduce_minute(uint8_t out_bpm[RED_N], uint8_t *out_spo2)
{
    uint8_t n = s_raw_n;
    if (n == 0) {
        memset(out_bpm, 0, RED_N);
        *out_spo2 = 0;
        return;
    }
    uint32_t spo2_sum = 0;
    uint8_t  spo2_n = 0;
    uint8_t prev = s_raw_bpm[0];
    for (int b = 0; b < RED_N; b++) {
        uint16_t a = (uint16_t)b * n / RED_N;
        uint16_t z = (uint16_t)(b + 1) * n / RED_N;
        if (z <= a) z = a + 1;
        if (z > n) z = n;

        uint8_t pick = s_raw_bpm[a];
        int best = 0;
        for (uint16_t i = a; i < z; i++) {
            int d = (int)s_raw_bpm[i] - (int)prev;
            if (d < 0) d = -d;
            if (d >= best) { best = d; pick = s_raw_bpm[i]; }
            if (s_raw_spo2[i]) { spo2_sum += s_raw_spo2[i]; spo2_n++; }
        }
        out_bpm[b] = pick;
        prev = pick;
    }
    *out_spo2 = spo2_n ? (uint8_t)(spo2_sum / spo2_n) : 0;
}

// RLE the 40 reduced BPM values (5 min × 8) into the blob.
static void commit_block(void)
{
    uint8_t flat[MINS_PER_BLOCK * RED_N];
    uint8_t spo2_sum = 0, spo2_n = 0;
    for (int m = 0; m < MINS_PER_BLOCK; m++) {
        memcpy(&flat[m * RED_N], s_red_bpm[m], RED_N);
        if (s_red_spo2[m]) { spo2_sum += s_red_spo2[m]; spo2_n++; }
    }

    // Worst case 40 runs; header is 6 bytes.
    uint8_t runs_bpm[40], runs_cnt[40];
    uint8_t n_runs = 0;
    for (int i = 0; i < 40; ) {
        uint8_t v = flat[i];
        uint8_t c = 1;
        while (i + c < 40 && flat[i + c] == v && c < 255) c++;
        runs_bpm[n_runs] = v;
        runs_cnt[n_runs] = c;
        n_runs++;
        i += c;
    }

    uint16_t need = 6 + n_runs * 2;
    if ((uint32_t)s_used + need > HR_METRICS_NVS_SIZE - 4) {
        ESP_LOGW(TAG, "blob full, dropping block");
        return;
    }

    uint32_t t_min = (uint32_t)(esp_timer_get_time() / 60000000ULL);
    uint8_t *p = s_blob + 4 + s_used;
    p[0] = (uint8_t)(t_min);
    p[1] = (uint8_t)(t_min >> 8);
    p[2] = (uint8_t)(t_min >> 16);
    p[3] = (uint8_t)(t_min >> 24);
    p[4] = n_runs;
    p[5] = spo2_n ? (uint8_t)(spo2_sum / spo2_n) : 0;
    for (uint8_t i = 0; i < n_runs; i++) {
        p[6 + i * 2]     = runs_bpm[i];
        p[6 + i * 2 + 1] = runs_cnt[i];
    }
    s_used += need;
    s_blob[2] = (uint8_t)((s_used > 0)); // block count low byte, rough
    blob_store();
    ESP_LOGI(TAG, "5-min block stored, %u runs, used=%u", n_runs, s_used);
}

static void try_flush_sd(void)
{
    if (s_used < HR_METRICS_FULL_MARK) return;

    FILE *f = fopen(SD_PATH, "ab");
    if (!f) {
        // Directory may not exist; one attempt to make it.
        // mkdir is fine if it already exists.
        mkdir("/sdcard/hr", 0775);
        f = fopen(SD_PATH, "ab");
    }
    if (!f) {
        s_sd_ok = false;
        ESP_LOGW(TAG, "SD flush skipped (no %s)", SD_PATH);
        return;
    }
    // Write used payload only (skip the 4-byte header; length-prefix the chunk).
    uint16_t n = s_used;
    fwrite(&n, 2, 1, f);
    fwrite(s_blob + 4, 1, s_used, f);
    fclose(f);

    memset(s_blob, 0, sizeof(s_blob));
    s_used = 0;
    blob_store();
    s_sd_ok = true;
    ESP_LOGI(TAG, "flushed %u bytes to SD", n);
}

esp_err_t hr_metrics_init(void)
{
    blob_load();
    s_raw_n = 0;
    s_mins = 0;
    s_inited = true;
    s_sd_ok = false;
    ESP_LOGI(TAG, "hr metrics ready, nvs used=%u", s_used);
    return ESP_OK;
}

void hr_metrics_push(uint8_t bpm, uint8_t spo2)
{
    if (!s_inited || bpm == 0) return;
    if (s_raw_n >= RAW_N) {
        // Window full: close the minute so we don't drop the new sample.
        hr_metrics_end_minute();
    }
    s_raw_bpm[s_raw_n] = bpm;
    s_raw_spo2[s_raw_n] = spo2;
    s_raw_n++;
}

void hr_metrics_end_minute(void)
{
    if (!s_inited) return;
    if (s_mins < MINS_PER_BLOCK) {
        reduce_minute(s_red_bpm[s_mins], &s_red_spo2[s_mins]);
        s_mins++;
    }
    s_raw_n = 0;

    if (s_mins >= MINS_PER_BLOCK) {
        commit_block();
        s_mins = 0;
        try_flush_sd();
    }
}

uint16_t hr_metrics_used(void) { return s_used; }
bool hr_metrics_sd_ok(void) { return s_sd_ok; }
