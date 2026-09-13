#include "siggen_play.h"
#include "siggen_ctrl.h"
#include "siggen_ledc.h"
#include "siggen_wavetable.h"
#include "siggen_adc_cal.h"
#include "precomputed_math/fast_inv_trig_i16.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

static const char* TAG = "siggen_play";
static int s_gpio = SIGGEN_DEFAULT_OUT_GPIO;
static uint32_t s_wave_hz = 1000;
static TaskHandle_t s_dds = nullptr;
static TaskHandle_t s_sweep = nullptr;
static volatile bool s_dds_run = false;
static siggen_adc_t s_adc;
static int s_adc_gpio = -1;

static int16_t amp_q15(void) {
    uint8_t a = siggen_get()->cfg.amplitude;
    if (a > 100) a = 100;
    return (int16_t)((a * 32767) / 100);
}

static void dds_task(void*) {
    siggen_state_t* st = siggen_get();
    int16_t buf[16];
    while (s_dds_run && st->cfg.running) {
        if (st->cfg.wave == SIGGEN_WAVE_SQUARE) {
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }
        uint32_t sr = st->cfg.sample_rate ? st->cfg.sample_rate : 8000;
        siggen_fill(st->cfg.wave, buf, 16, &st->phase, st->phase_inc,
                    amp_q15(), st->cfg.duty_percent);
        uint32_t us = 1000000u / sr;
        if (us < 20) us = 20;
        for (int i = 0; i < 16 && s_dds_run; i++) {
            siggen_ledc_apply_sample(st, buf[i]);
            esp_rom_delay_us(us);
        }
    }
    s_dds = nullptr;
    vTaskDelete(NULL);
}

static void ensure_dds(void) {
    siggen_state_t* st = siggen_get();
    if (st->cfg.wave == SIGGEN_WAVE_SQUARE) return;
    if (s_dds) return;
    s_dds_run = true;
    xTaskCreatePinnedToCore(dds_task, "sig_dds", 3072, NULL, 8, &s_dds, 0);
}

esp_err_t siggen_play(int wave, uint32_t freq_hz, uint8_t duty_pct, int gpio, uint8_t amp_pct) {
    if (wave < 0 || wave > 4) wave = SIGGEN_WAVE_SINE;
    if (!freq_hz) freq_hz = 1000;
    if (freq_hz > 200000) freq_hz = 200000;
    if (duty_pct > 100) duty_pct = 100;
    if (!duty_pct) duty_pct = 50;
    if (amp_pct > 100) amp_pct = 100;
    if (!amp_pct) amp_pct = 80;
    if (gpio <= 0) gpio = SIGGEN_DEFAULT_OUT_GPIO;

    s_dds_run = false;
    if (s_dds) {
        vTaskDelay(pdMS_TO_TICKS(15));
        s_dds = nullptr;
    }
    siggen_stop();

    s_gpio = gpio;
    s_wave_hz = freq_hz;

    siggen_cfg_t c = {};
    c.wave = (siggen_wave_t)wave;
    c.out = SIGGEN_OUT_LEDC;
    c.gpio = gpio;
    c.duty_percent = duty_pct;
    c.amplitude = amp_pct;
    c.sample_rate = 8000;
    /* Square: LEDC hardware frequency = wave. Analog-ish: high PWM carrier + duty DDS. */
    c.freq_hz = (c.wave == SIGGEN_WAVE_SQUARE) ? freq_hz : 100000;
    c.running = false;

    esp_err_t e = siggen_configure(&c);
    if (e != ESP_OK) return e;

    siggen_state_t* st = siggen_get();
    st->cfg.freq_hz = freq_hz;          /* snapshot shows the *signal* Hz */
    st->phase_inc = pcm_phase_inc(freq_hz, st->cfg.sample_rate);
    e = siggen_start();
    if (e != ESP_OK) return e;
    ensure_dds();
    ESP_LOGI(TAG, "play wave=%d f=%lu duty=%u gpio=%d amp=%u",
             wave, (unsigned long)freq_hz, duty_pct, gpio, amp_pct);
    return ESP_OK;
}

esp_err_t siggen_play_stop(void) {
    s_dds_run = false;
    if (s_sweep) { /* sweep task checks running flag */ }
    return siggen_stop();
}

esp_err_t siggen_play_set_freq(uint32_t freq_hz) {
    if (!freq_hz) return ESP_ERR_INVALID_ARG;
    s_wave_hz = freq_hz;
    siggen_state_t* st = siggen_get();
    if (st->cfg.wave == SIGGEN_WAVE_SQUARE)
        return siggen_set_freq(freq_hz);
    st->cfg.freq_hz = freq_hz;
    st->phase_inc = pcm_phase_inc(freq_hz, st->cfg.sample_rate ? st->cfg.sample_rate : 8000);
    return ESP_OK;
}

esp_err_t siggen_play_set_duty(uint8_t duty_pct) {
    return siggen_set_duty(duty_pct);
}

esp_err_t siggen_play_set_amp(uint8_t amp_pct) {
    if (amp_pct > 100) amp_pct = 100;
    siggen_get()->cfg.amplitude = amp_pct;
    return ESP_OK;
}

static void sweep_task(void* arg) {
    uint32_t* p = (uint32_t*)arg;
    uint32_t f0 = p[0], f1 = p[1], ms = p[2];
    free(p);
    if (ms < 50) ms = 50;
    const int steps = 40;
    uint32_t dt = ms / steps;
    if (dt < 10) dt = 10;
    for (int i = 0; i <= steps && siggen_get()->cfg.running; i++) {
        uint32_t f = f0 + (uint32_t)(((int64_t)(f1 - f0) * i) / steps);
        siggen_play_set_freq(f);
        vTaskDelay(pdMS_TO_TICKS(dt));
    }
    s_sweep = nullptr;
    vTaskDelete(NULL);
}

esp_err_t siggen_play_sweep(uint32_t f0, uint32_t f1, uint32_t ms) {
    if (!siggen_get()->cfg.running) {
        esp_err_t e = siggen_play(SIGGEN_WAVE_SINE, f0, 50, s_gpio, 80);
        if (e != ESP_OK) return e;
    }
    uint32_t* p = (uint32_t*)malloc(3 * sizeof(uint32_t));
    if (!p) return ESP_ERR_NO_MEM;
    p[0] = f0; p[1] = f1; p[2] = ms;
    xTaskCreate(sweep_task, "sig_sweep", 2048, p, 4, &s_sweep);
    return ESP_OK;
}

const siggen_cfg_t* siggen_play_cfg(void) { return siggen_cfg_snapshot(); }
int siggen_play_gpio(void) { return s_gpio; }
int siggen_scope_gpio(void) {
    return s_adc_gpio > 0 ? s_adc_gpio : SIGGEN_DEFAULT_SCOPE_GPIO;
}

static bool adc_ready(int gpio) {
    if (s_adc_gpio == gpio && s_adc.oneshot) return true;
    if (s_adc.oneshot) siggen_adc_deinit(&s_adc);
    if (siggen_adc_init(&s_adc, 1, gpio) != ESP_OK) {
        /* try unit 2 (will fail under WiFi often) */
        if (siggen_adc_init(&s_adc, 2, gpio) != ESP_OK) return false;
    }
    s_adc_gpio = gpio;
    return true;
}

int siggen_adc_once(int gpio) {
    if (gpio <= 0) gpio = SIGGEN_DEFAULT_SCOPE_GPIO;
    if (!adc_ready(gpio)) return -1;
    int mv = 0;
    if (siggen_adc_read_mv(&s_adc, &mv) != ESP_OK) return -1;
    return mv;
}

int siggen_scope_cap(int gpio, int n, int16_t* mv, int16_t* raw) {
    if (gpio <= 0) gpio = SIGGEN_DEFAULT_SCOPE_GPIO;
    if (n <= 0) return 0;
    if (n > 512) n = 512;
    if (!adc_ready(gpio)) return -1;
    int got = 0;
    for (int i = 0; i < n; i++) {
        int r = 0, m = 0;
        if (siggen_adc_read_raw(&s_adc, &r) != ESP_OK) break;
        siggen_adc_read_mv(&s_adc, &m);
        if (raw) raw[i] = (int16_t)r;
        if (mv)  mv[i]  = (int16_t)m;
        got++;
        esp_rom_delay_us(40); /* ~25 kS/s oneshot */
    }
    return got;
}

static uint32_t parse_freq_token(const char* s) {
    if (!s || !s[0]) return 0;
    char* end = NULL;
    double v = strtod(s, &end);
    if (end && (*end == 'k' || *end == 'K')) v *= 1000.0;
    else if (end && (*end == 'm' || *end == 'M')) v *= 1000000.0;
    if (v < 0) v = 0;
    if (v > 40000000.0) v = 40000000.0;
    return (uint32_t)(v + 0.5);
}

int siggen_corz_cmd(const char* cmd, char* out, int out_max) {
    if (!cmd) return -1;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    char tmp[96];
    strncpy(tmp, cmd, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = 0;
    /* trim */
    size_t L = strlen(tmp);
    while (L && (tmp[L-1]=='\n' || tmp[L-1]=='\r' || tmp[L-1]==' ')) tmp[--L] = 0;
    if (!L) return -1;

    auto say = [&](const char* m) {
        if (out && out_max > 0) snprintf(out, out_max, "%s", m);
    };

    if (!strcasecmp(tmp, "s") || !strcasecmp(tmp, "sine")) {
        siggen_play(SIGGEN_WAVE_SINE, s_wave_hz, 50, s_gpio, 80);
        say("sine"); return 0;
    }
    if (!strcasecmp(tmp, "r") || !strcasecmp(tmp, "square") || !strcasecmp(tmp, "rect")) {
        siggen_play(SIGGEN_WAVE_SQUARE, s_wave_hz, siggen_get()->cfg.duty_percent, s_gpio, 80);
        say("square"); return 0;
    }
    if (!strcasecmp(tmp, "t") || !strcasecmp(tmp, "tri") || !strcasecmp(tmp, "triangle")) {
        siggen_play(SIGGEN_WAVE_TRIANGLE, s_wave_hz, 50, s_gpio, 80);
        say("triangle"); return 0;
    }
    if (!strcasecmp(tmp, "w") || !strcasecmp(tmp, "saw")) {
        siggen_play(SIGGEN_WAVE_SAW, s_wave_hz, 50, s_gpio, 80);
        say("saw"); return 0;
    }
    if (!strcasecmp(tmp, "stop") || !strcmp(tmp, ".") || !strcasecmp(tmp, "end")) {
        siggen_play_stop();
        say("stop"); return 0;
    }
    if (tmp[0] == 'p' || tmp[0] == 'P') {
        int d = atoi(tmp + 1);
        siggen_play_set_duty((uint8_t)d);
        say("duty"); return 0;
    }
    if (tmp[0] == 'a' || tmp[0] == 'A') {
        int lvl = atoi(tmp + 1); /* corz 1..4 → 12/25/50/100 */
        uint8_t amp = 80;
        if (lvl == 1) amp = 12;
        else if (lvl == 2) amp = 25;
        else if (lvl == 3) amp = 50;
        else if (lvl >= 4) amp = 100;
        else if (lvl > 4 && lvl <= 100) amp = (uint8_t)lvl;
        siggen_play_set_amp(amp);
        say("amp"); return 0;
    }
    if (!strncasecmp(tmp, "scope", 5)) {
        int pin = SIGGEN_DEFAULT_SCOPE_GPIO;
        const char* p = tmp + 5;
        while (*p == ' ') p++;
        if (*p) pin = atoi(p);
        int16_t buf[8];
        int got = siggen_scope_cap(pin, 8, buf, NULL);
        int last = got > 0 ? (int)buf[got - 1] : -1;
        int mn = last, mx = last;
        for (int i = 1; i < got; i++) {
            if (buf[i] < mn) mn = buf[i];
            if (buf[i] > mx) mx = buf[i];
        }
        if (out && out_max > 0)
            snprintf(out, out_max, "scope GPIO%d n=%d last=%d min=%d max=%d mV",
                     pin, got, last, mn, mx);
        return got < 0 ? -1 : 0;
    }
    if (!strncasecmp(tmp, "sweep ", 6)) {
        char a[24], b[24], c[24];
        a[0]=b[0]=c[0]=0;
        sscanf(tmp + 6, "%23s %23s %23s", a, b, c);
        uint32_t f0 = parse_freq_token(a);
        uint32_t f1 = parse_freq_token(b);
        uint32_t ms = (uint32_t)atoi(c);
        if (!ms) ms = 2000;
        siggen_play_sweep(f0, f1, ms);
        say("sweep"); return 0;
    }
    if (!strncasecmp(tmp, "pin ", 4)) {
        s_gpio = atoi(tmp + 4);
        say("pin"); return 0;
    }
    /* bare frequency: 2000 / 2k / 1.25m / +100 */
    if (tmp[0] == '+' || tmp[0] == '-' || isdigit((unsigned char)tmp[0])) {
        uint32_t f = parse_freq_token(tmp[0]=='+' || tmp[0]=='-' ? tmp : tmp);
        if (tmp[0] == '+' || tmp[0] == '-') {
            int32_t d = (tmp[0]=='-') ? -(int32_t)parse_freq_token(tmp+1) : (int32_t)parse_freq_token(tmp+1);
            f = (uint32_t)((int32_t)s_wave_hz + d);
        }
        siggen_play_set_freq(f ? f : 1);
        say("freq"); return 0;
    }
    say("unknown");
    return -1;
}
