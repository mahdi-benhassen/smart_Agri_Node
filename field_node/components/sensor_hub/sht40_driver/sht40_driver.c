/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * sht40_driver.c — SHT40 I2C temperature & humidity sensor driver
 */

#include "sht40_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "SHT40";

static i2c_port_t s_port = I2C_NUM_0;
static uint8_t    s_addr = SHT40_I2C_ADDR;
static bool       s_initialized = false;

#define I2C_TIMEOUT_MS  100

/* ── Internal: CRC-8 (polynomial 0x31, init 0xFF) for Sensirion ──── */

static uint8_t sht40_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* ── Internal: I2C write command ───────────────────────────────────── */

static esp_err_t sht40_write_cmd(uint8_t cmd)
{
    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(handle);
    i2c_master_write_byte(handle, (s_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(handle, cmd, true);
    i2c_master_stop(handle);

    esp_err_t ret = i2c_master_cmd_begin(s_port, handle, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(handle);

    return ret;
}

/* ── Internal: I2C read bytes ──────────────────────────────────────── */

static esp_err_t sht40_read_bytes(uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(handle);
    i2c_master_write_byte(handle, (s_addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(handle, buf, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(handle, &buf[len - 1], I2C_MASTER_NACK);
    i2c_master_stop(handle);

    esp_err_t ret = i2c_master_cmd_begin(s_port, handle, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(handle);

    return ret;
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t sht40_init(const sht40_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    s_port = config->i2c_port;
    s_addr = config->i2c_addr ? config->i2c_addr : SHT40_I2C_ADDR;

    /* Issue soft reset */
    esp_err_t ret = sht40_write_cmd(SHT40_CMD_SOFT_RESET);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Soft reset failed: %s (addr=0x%02X, port=%d)",
                 esp_err_to_name(ret), s_addr, s_port);
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(2));  /* 1 ms reset time */
    s_initialized = true;

    ESP_LOGI(TAG, "Initialized on I2C%d addr=0x%02X", s_port, s_addr);
    return ESP_OK;
}

esp_err_t sht40_read(sht40_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    data->valid = false;

    /* Send high-precision measurement command */
    esp_err_t ret = sht40_write_cmd(SHT40_CMD_MEASURE_HP);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Measure cmd failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Wait for measurement */
    vTaskDelay(pdMS_TO_TICKS(SHT40_MEAS_DURATION_MS));

    /* Read 6 bytes: [Temp MSB, Temp LSB, CRC, Hum MSB, Hum LSB, CRC] */
    uint8_t buf[6];
    ret = sht40_read_bytes(buf, 6);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Verify CRC for temperature */
    if (sht40_crc8(&buf[0], 2) != buf[2]) {
        ESP_LOGW(TAG, "Temp CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    /* Verify CRC for humidity */
    if (sht40_crc8(&buf[3], 2) != buf[5]) {
        ESP_LOGW(TAG, "Hum CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    /* Convert raw values */
    uint16_t raw_temp = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t raw_hum  = ((uint16_t)buf[3] << 8) | buf[4];

    data->temperature = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
    data->humidity    = -6.0f + 125.0f * ((float)raw_hum / 65535.0f);

    /* Clamp humidity */
    if (data->humidity < 0.0f) data->humidity = 0.0f;
    if (data->humidity > 100.0f) data->humidity = 100.0f;

    /* Sanity check */
    if (data->temperature < -40.0f || data->temperature > 125.0f) {
        ESP_LOGW(TAG, "Temp out of range: %.1f °C", data->temperature);
        return ESP_ERR_INVALID_RESPONSE;
    }

    data->valid = true;
    ESP_LOGD(TAG, "T=%.2f°C H=%.2f%%", data->temperature, data->humidity);
    return ESP_OK;
}

esp_err_t sht40_soft_reset(void)
{
    return sht40_write_cmd(SHT40_CMD_SOFT_RESET);
}

esp_err_t sht40_read_serial(uint32_t *serial)
{
    if (serial == NULL || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = sht40_write_cmd(SHT40_CMD_SERIAL);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(2));

    uint8_t buf[6];
    ret = sht40_read_bytes(buf, 6);
    if (ret != ESP_OK) return ret;

    if (sht40_crc8(&buf[0], 2) != buf[2] || sht40_crc8(&buf[3], 2) != buf[5]) {
        return ESP_ERR_INVALID_CRC;
    }

    *serial = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
              ((uint32_t)buf[3] << 8)  | buf[4];
    return ESP_OK;
}

esp_err_t sht40_heater_pulse(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = sht40_write_cmd(SHT40_CMD_HEATER_LO_1S);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(1100));  /* Wait for heater cycle */
    return ESP_OK;
}

void sht40_deinit(void)
{
    s_initialized = false;
    ESP_LOGI(TAG, "Deinitialized");
}
