/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * rain_gauge_driver.h — Tipping-bucket rain gauge via GPIO interrupt
 */

#ifndef RAIN_GAUGE_DRIVER_H
#define RAIN_GAUGE_DRIVER_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RAIN_GAUGE_MM_PER_PULSE     0.2f    /* 0.2 mm per tip */
#define RAIN_GAUGE_DEBOUNCE_MS      50      /* 50 ms debounce */

typedef struct {
    float       rain_mm;            /* Cumulative rainfall in mm */
    uint32_t    pulse_count;        /* Raw pulse count */
    bool        valid;
} rain_gauge_data_t;

typedef struct {
    gpio_num_t  pulse_pin;          /* GPIO connected to reed switch */
    gpio_int_type_t edge;           /* GPIO_INTR_NEGEDGE or POSEDGE */
} rain_gauge_config_t;

esp_err_t rain_gauge_init(const rain_gauge_config_t *config);
esp_err_t rain_gauge_read(rain_gauge_data_t *data);
esp_err_t rain_gauge_reset(void);
uint32_t  rain_gauge_get_count(void);
void      rain_gauge_set_count(uint32_t count);
void      rain_gauge_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* RAIN_GAUGE_DRIVER_H */
