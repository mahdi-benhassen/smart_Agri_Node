/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * soil_moisture_driver.h — Capacitive soil moisture sensor via ADC
 */

#ifndef SOIL_MOISTURE_DRIVER_H
#define SOIL_MOISTURE_DRIVER_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOIL_MOISTURE_MEDIAN_WINDOW  5
#define SOIL_MOISTURE_MAX_PROBES     4

typedef struct {
    float       vwc_pct;            /* Volumetric water content 0–60% */
    float       soil_temp_c;        /* Soil temperature (if available) */
    uint16_t    raw_adc;            /* Raw ADC value */
    bool        valid;
} soil_moisture_data_t;

typedef struct {
    adc_unit_t   adc_unit;
    adc_channel_t channel;          /* ADC channel for moisture probe */
    adc_channel_t temp_channel;     /* ADC channel for soil temp (or -1) */
    adc_atten_t  attenuation;       /* ADC attenuation (default 11dB) */
    /* Calibration: VWC = (raw_adc - cal_dry) / (cal_wet - cal_dry) * 60.0 */
    uint16_t     cal_dry;           /* ADC value at 0% VWC */
    uint16_t     cal_wet;           /* ADC value at 60% VWC */
} soil_moisture_config_t;

esp_err_t soil_moisture_init(const soil_moisture_config_t *config);
esp_err_t soil_moisture_read(soil_moisture_data_t *data);
void      soil_moisture_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SOIL_MOISTURE_DRIVER_H */
