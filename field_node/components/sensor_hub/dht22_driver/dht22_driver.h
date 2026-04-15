/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * dht22_driver.h — DHT22 (AM2302) temperature & humidity sensor driver
 * 1-Wire GPIO bit-bang protocol with microsecond timing.
 */

#ifndef DHT22_DRIVER_H
#define DHT22_DRIVER_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuration ─────────────────────────────────────────────────── */
#define DHT22_MIN_INTERVAL_MS   2000    /* Minimum 2s between reads */
#define DHT22_TIMEOUT_US        1000    /* Bit read timeout */
#define DHT22_DATA_BITS         40      /* 16 hum + 16 temp + 8 checksum */

/* ── Data Structure ────────────────────────────────────────────────── */
typedef struct {
    float    temperature;       /* °C, range: -40 to +80 */
    float    humidity;          /* %, range: 0 to 100 */
    uint32_t last_read_ms;      /* Timestamp of last successful read */
    bool     valid;             /* true if last read was valid */
} dht22_data_t;

typedef struct {
    gpio_num_t data_pin;        /* GPIO pin connected to DHT22 data */
} dht22_config_t;

/* ── API Functions ─────────────────────────────────────────────────── */

/**
 * @brief Initialize DHT22 driver.
 *
 * @param config  GPIO configuration
 * @return ESP_OK on success
 */
esp_err_t dht22_init(const dht22_config_t *config);

/**
 * @brief Read temperature and humidity from DHT22.
 *
 * Enforces minimum 2s interval between reads.
 * Includes CRC-8 checksum validation.
 *
 * @param data  Output data structure
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on no response,
 *         ESP_ERR_INVALID_CRC on checksum failure
 */
esp_err_t dht22_read(dht22_data_t *data);

/**
 * @brief Deinitialize DHT22 driver, release GPIO.
 */
void dht22_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* DHT22_DRIVER_H */
