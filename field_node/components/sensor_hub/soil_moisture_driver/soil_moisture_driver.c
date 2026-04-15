/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * soil_moisture_driver.c — Capacitive soil moisture sensor via ADC oneshot
 */

#include "soil_moisture_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "SOIL_MOIST";

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static soil_moisture_config_t    s_config;
static bool                      s_initialized = false;

/* ── Internal: Median filter ───────────────────────────────────────── */

static int compare_u16(const void *a, const void *b)
{
    uint16_t va = *(const uint16_t *)a;
    uint16_t vb = *(const uint16_t *)b;
    return (va > vb) - (va < vb);
}

static uint16_t median_filter(adc_channel_t ch)
{
    uint16_t samples[SOIL_MOISTURE_MEDIAN_WINDOW];

    for (int i = 0; i < SOIL_MOISTURE_MEDIAN_WINDOW; i++) {
        int raw = 0;
        esp_err_t ret = adc_oneshot_read(s_adc_handle, ch, &raw);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "ADC read error: %s", esp_err_to_name(ret));
            raw = 0;
        }
        samples[i] = (uint16_t)raw;
        vTaskDelay(pdMS_TO_TICKS(5));     /* Small delay between samples */
    }

    /* Sort and return median */
    qsort(samples, SOIL_MOISTURE_MEDIAN_WINDOW, sizeof(uint16_t), compare_u16);
    return samples[SOIL_MOISTURE_MEDIAN_WINDOW / 2];
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t soil_moisture_init(const soil_moisture_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(soil_moisture_config_t));

    /* Set sane calibration defaults if not provided */
    if (s_config.cal_dry == 0 && s_config.cal_wet == 0) {
        s_config.cal_dry = 3500;    /* Typical dry value (12-bit ADC) */
        s_config.cal_wet = 1500;    /* Typical wet value */
    }

    /* Initialize ADC oneshot driver */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = config->adc_unit,
    };

    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure moisture channel */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = config->attenuation,
        .bitwidth = ADC_BITWIDTH_12,
    };

    ret = adc_oneshot_config_channel(s_adc_handle, config->channel, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return ret;
    }

    /* Configure temperature channel if provided */
    if ((int)config->temp_channel >= 0) {
        ret = adc_oneshot_config_channel(s_adc_handle, config->temp_channel, &chan_cfg);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Soil temp channel config failed: %s", esp_err_to_name(ret));
            /* Non-fatal — continue without soil temp */
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Initialized ADC unit %d, ch %d, cal: dry=%u wet=%u",
             config->adc_unit, config->channel, s_config.cal_dry, s_config.cal_wet);
    return ESP_OK;
}

esp_err_t soil_moisture_read(soil_moisture_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_adc_handle == NULL) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    data->valid = false;

    /* Read moisture with median filter */
    uint16_t raw = median_filter(s_config.channel);
    data->raw_adc = raw;

    /* Convert to VWC percentage using calibration */
    float range = (float)((int32_t)s_config.cal_dry - (int32_t)s_config.cal_wet);
    if (range == 0.0f) {
        ESP_LOGE(TAG, "Invalid calibration (dry == wet)");
        return ESP_ERR_INVALID_STATE;
    }

    /* Inverted: lower ADC = wetter (capacitive sensor) */
    float vwc = ((float)((int32_t)s_config.cal_dry - (int32_t)raw) / range) * 60.0f;

    /* Clamp to valid range */
    if (vwc < 0.0f) vwc = 0.0f;
    if (vwc > 60.0f) vwc = 60.0f;
    data->vwc_pct = vwc;

    /* Read soil temperature if channel available */
    data->soil_temp_c = 0.0f;
    if ((int)s_config.temp_channel >= 0) {
        uint16_t temp_raw = median_filter(s_config.temp_channel);
        /* LM35-style: 10 mV/°C, ADC 12-bit, Vref ~3.3V */
        float voltage = (float)temp_raw * 3.3f / 4095.0f;
        data->soil_temp_c = voltage * 100.0f;  /* LM35: 10mV/°C → V*100=°C */
    }

    data->valid = true;
    ESP_LOGD(TAG, "VWC=%.1f%% raw=%u soil_temp=%.1f°C", data->vwc_pct, raw, data->soil_temp_c);
    return ESP_OK;
}

void soil_moisture_deinit(void)
{
    if (s_adc_handle != NULL) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
    }
    s_initialized = false;
    ESP_LOGI(TAG, "Deinitialized");
}
