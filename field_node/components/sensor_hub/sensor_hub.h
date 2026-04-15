/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * sensor_hub.h — Sensor orchestrator: initializes all sensors,
 *                runs parallel acquisition, applies median filter & calibration
 */

#ifndef SENSOR_HUB_H
#define SENSOR_HUB_H

#include <stdint.h>
#include "esp_err.h"
#include "agri_data_model.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── GPIO Pin Assignments (configurable via NVS) ───────────────────── */
typedef struct {
    /* I2C bus */
    i2c_port_t  i2c_port;
    gpio_num_t  i2c_sda;
    gpio_num_t  i2c_scl;
    uint32_t    i2c_freq_hz;

    /* DHT22 */
    gpio_num_t  dht22_pin;

    /* Soil moisture ADC */
    adc_unit_t     soil_adc_unit;
    adc_channel_t  soil_adc_channel;
    adc_channel_t  soil_temp_channel;   /* -1 if not used */
    uint16_t       soil_cal_dry;
    uint16_t       soil_cal_wet;

    /* NPK RS-485 */
    uart_port_t npk_uart_port;
    gpio_num_t  npk_tx_pin;
    gpio_num_t  npk_rx_pin;
    gpio_num_t  npk_de_re_pin;

    /* Rain gauge */
    gpio_num_t  rain_pin;

    /* Calibration */
    agri_calibration_t calibration;
} sensor_hub_config_t;

/**
 * @brief Initialize all sensor drivers.
 */
esp_err_t sensor_hub_init(const sensor_hub_config_t *config);

/**
 * @brief Acquire all sensor readings in parallel and populate data struct.
 *
 * Runs each sensor read as a FreeRTOS task, waits for completion,
 * applies median filter and NVS calibration, populates agri_sensor_data_t.
 *
 * @param data  Output sensor data struct (must be pre-initialized)
 * @param alarm_flags  Updated alarm flags (OR'd with existing)
 * @return ESP_OK if at least some sensors read successfully
 */
esp_err_t sensor_hub_acquire_all(agri_sensor_data_t *data, uint16_t *alarm_flags);

/**
 * @brief Deinitialize all sensor drivers, release resources.
 */
void sensor_hub_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_HUB_H */
