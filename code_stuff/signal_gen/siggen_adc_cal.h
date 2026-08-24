#pragma once
// ADC + curve-fitting calibration (ESP32-S3) for scope / loopback measure.
// https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32s3/api-reference/peripherals/adc_calibration.html
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void*  oneshot;   // adc_oneshot_unit_handle_t
    void*  cali;      // adc_cali_handle_t
    int    channel;   // adc_channel_t as int
    int    unit;      // ADC_UNIT_1 / 2
    bool   cali_ok;
} siggen_adc_t;

// unit: 1 or 2; gpio must be a valid ADC pin for that unit
esp_err_t siggen_adc_init(siggen_adc_t* a, int unit, int gpio);
void      siggen_adc_deinit(siggen_adc_t* a);

// Returns calibrated millivolts; on failure returns negative esp_err style via *out_mv = -1
esp_err_t siggen_adc_read_mv(siggen_adc_t* a, int* out_mv);
esp_err_t siggen_adc_read_raw(siggen_adc_t* a, int* out_raw);

#ifdef __cplusplus
}
#endif
