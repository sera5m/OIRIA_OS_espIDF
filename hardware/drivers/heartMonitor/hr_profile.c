#include "hr_profile.h"
#include <string.h>
#include <math.h>

// Welford running mean/variance. Fixed, no allocation.
typedef struct {
    uint32_t n;
    double   mean;
    double   m2;
} welford_t;

static welford_t s_bpm;
static welford_t s_spo2;

// Last few beat intervals for RMSSD.
#define HRV_N 32
static uint16_t s_rr[HRV_N];
static uint8_t  s_rr_n;
static uint8_t  s_rr_i;

static void welford_push(welford_t *w, double x)
{
    w->n++;
    double d = x - w->mean;
    w->mean += d / (double)w->n;
    w->m2 += d * (x - w->mean);
}

static double welford_sd(const welford_t *w)
{
    if (w->n < 2) return 0;
    return sqrt(w->m2 / (double)(w->n - 1));
}

esp_err_t hr_profile_init(void)
{
    memset(&s_bpm, 0, sizeof s_bpm);
    memset(&s_spo2, 0, sizeof s_spo2);
    s_rr_n = 0;
    s_rr_i = 0;
    return ESP_OK;
}

void hr_profile_push(uint8_t bpm, uint8_t spo2, uint32_t interval_ms)
{
    if (bpm) welford_push(&s_bpm, bpm);
    if (spo2) welford_push(&s_spo2, spo2);
    if (interval_ms >= 300 && interval_ms <= 2000) {
        s_rr[s_rr_i] = (uint16_t)interval_ms;
        s_rr_i = (uint8_t)((s_rr_i + 1) % HRV_N);
        if (s_rr_n < HRV_N) s_rr_n++;
    }
}

static uint16_t rmssd(void)
{
    if (s_rr_n < 4) return 0;
    // Walk the ring in order.
    int start = (s_rr_n == HRV_N) ? s_rr_i : 0;
    double sum = 0;
    int n = 0;
    uint16_t prev = s_rr[start];
    for (int i = 1; i < s_rr_n; i++) {
        uint16_t cur = s_rr[(start + i) % HRV_N];
        double d = (double)cur - (double)prev;
        sum += d * d;
        n++;
        prev = cur;
    }
    if (n == 0) return 0;
    return (uint16_t)sqrt(sum / n);
}

void hr_profile_get(hr_profile_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->n = (uint16_t)(s_bpm.n > 65535 ? 65535 : s_bpm.n);
    double bm = s_bpm.mean;
    double bs = welford_sd(&s_bpm);
    out->bpm_mean = (uint8_t)(bm + 0.5);
    out->bpm_sd = (uint8_t)(bs + 0.5);
    int lo = (int)(bm - 2.0 * bs);
    int hi = (int)(bm + 2.0 * bs);
    if (lo < 30) lo = 30;
    if (hi > 200) hi = 200;
    if (hi < lo) hi = lo;
    out->bpm_lo = (uint8_t)lo;
    out->bpm_hi = (uint8_t)hi;
    if (s_spo2.n) {
        out->spo2_mean = (uint8_t)(s_spo2.mean + 0.5);
        out->spo2_sd = (uint8_t)(welford_sd(&s_spo2) + 0.5);
    }
    out->hrv_rmssd_ms = rmssd();
    out->hrv_valid = s_rr_n >= 4;
}

int hr_profile_bpm_band(uint8_t bpm)
{
    if (s_bpm.n < 8 || !bpm) return 0;
    hr_profile_t p;
    hr_profile_get(&p);
    if (bpm < p.bpm_lo) return -1;
    if (bpm > p.bpm_hi) return 1;
    return 0;
}
