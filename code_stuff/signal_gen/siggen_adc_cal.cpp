#include "siggen_adc_cal.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char* TAG = "siggen_adc";

esp_err_t siggen_adc_init(siggen_adc_t* a, int unit, int gpio) {
    if (!a) return ESP_ERR_INVALID_ARG;
    a->oneshot = nullptr;
    a->cali = nullptr;
    a->cali_ok = false;
    a->unit = unit;

    adc_oneshot_unit_handle_t handle = nullptr;
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = (unit == 2) ? ADC_UNIT_2 : ADC_UNIT_1,
    };
    esp_err_t e = adc_oneshot_new_unit(&init_cfg, &handle);
    if (e != ESP_OK) return e;
    a->oneshot = handle;

    adc_unit_t u;
    adc_channel_t ch;
    e = adc_oneshot_io_to_channel(gpio, &u, &ch);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "gpio %d is not ADC", gpio);
        adc_oneshot_del_unit(handle);
        a->oneshot = nullptr;
        return e;
    }
    a->channel = (int)ch;

    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    e = adc_oneshot_config_channel(handle, ch, &ch_cfg);
    if (e != ESP_OK) return e;

    // Curve fitting (S3)
    adc_cali_handle_t cali = nullptr;
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = init_cfg.unit_id,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    e = adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali);
    if (e == ESP_OK) {
        a->cali = cali;
        a->cali_ok = true;
        ESP_LOGI(TAG, "ADC unit%d ch%d gpio%d cali=curve", unit, (int)ch, gpio);
    } else {
        ESP_LOGW(TAG, "ADC cali unavailable (%s) — raw only", esp_err_to_name(e));
        a->cali_ok = false;
    }
    return ESP_OK;
}

void siggen_adc_deinit(siggen_adc_t* a) {
    if (!a) return;
    if (a->cali_ok && a->cali) {
        adc_cali_delete_scheme_curve_fitting((adc_cali_handle_t)a->cali);
        a->cali = nullptr;
        a->cali_ok = false;
    }
    if (a->oneshot) {
        adc_oneshot_del_unit((adc_oneshot_unit_handle_t)a->oneshot);
        a->oneshot = nullptr;
    }
}

esp_err_t siggen_adc_read_raw(siggen_adc_t* a, int* out_raw) {
    if (!a || !a->oneshot || !out_raw) return ESP_ERR_INVALID_ARG;
    return adc_oneshot_read((adc_oneshot_unit_handle_t)a->oneshot,
                            (adc_channel_t)a->channel, out_raw);
}

esp_err_t siggen_adc_read_mv(siggen_adc_t* a, int* out_mv) {
    if (!a || !out_mv) return ESP_ERR_INVALID_ARG;
    int raw = 0;
    esp_err_t e = siggen_adc_read_raw(a, &raw);
    if (e != ESP_OK) return e;
    if (a->cali_ok && a->cali) {
        return adc_cali_raw_to_voltage((adc_cali_handle_t)a->cali, raw, out_mv);
    }
    // crude fallback: 12-bit full scale ~ 3100 mV at 11 dB/12 dB atten
    *out_mv = (raw * 3100) / 4095;
    return ESP_OK;
}
