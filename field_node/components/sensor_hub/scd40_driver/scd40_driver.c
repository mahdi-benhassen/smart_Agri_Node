/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * scd40_driver.c — SCD40 I2C CO₂ sensor driver
 */

#include "scd40_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "SCD40";

static i2c_port_t s_port = I2C_NUM_0;
static uint8_t    s_addr = SCD40_I2C_ADDR;
static bool       s_initialized = false;

#define I2C_TIMEOUT_MS  100

/* ── Internal: CRC-8 (Sensirion, polynomial 0x31, init 0xFF) ──────── */

static uint8_t scd40_crc8(const uint8_t *data, size_t len)
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

/* ── Internal: Write 16-bit command ────────────────────────────────── */

static esp_err_t scd40_write_cmd(uint16_t cmd)
{
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };

    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    if (handle == NULL) return ESP_ERR_NO_MEM;

    i2c_master_start(handle);
    i2c_master_write_byte(handle, (s_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(handle, buf, 2, true);
    i2c_master_stop(handle);

    esp_err_t ret = i2c_master_cmd_begin(s_port, handle, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(handle);
    return ret;
}

/* ── Internal: Write command with 16-bit argument ──────────────────── */

static esp_err_t scd40_write_cmd_with_arg(uint16_t cmd, uint16_t arg)
{
    uint8_t buf[5];
    buf[0] = (uint8_t)(cmd >> 8);
    buf[1] = (uint8_t)(cmd & 0xFF);
    buf[2] = (uint8_t)(arg >> 8);
    buf[3] = (uint8_t)(arg & 0xFF);
    buf[4] = scd40_crc8(&buf[2], 2);

    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    if (handle == NULL) return ESP_ERR_NO_MEM;

    i2c_master_start(handle);
    i2c_master_write_byte(handle, (s_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(handle, buf, 5, true);
    i2c_master_stop(handle);

    esp_err_t ret = i2c_master_cmd_begin(s_port, handle, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(handle);
    return ret;
}

/* ── Internal: Read response words ─────────────────────────────────── */

static esp_err_t scd40_read_words(uint16_t *words, size_t count)
{
    size_t buf_len = count * 3;  /* Each word: 2 data bytes + 1 CRC */
    uint8_t buf[18];             /* Max 6 words */

    if (buf_len > sizeof(buf)) return ESP_ERR_INVALID_SIZE;

    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    if (handle == NULL) return ESP_ERR_NO_MEM;

    i2c_master_start(handle);
    i2c_master_write_byte(handle, (s_addr << 1) | I2C_MASTER_READ, true);
    if (buf_len > 1) {
        i2c_master_read(handle, buf, buf_len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(handle, &buf[buf_len - 1], I2C_MASTER_NACK);
    i2c_master_stop(handle);

    esp_err_t ret = i2c_master_cmd_begin(s_port, handle, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(handle);

    if (ret != ESP_OK) return ret;

    /* Parse and verify each word */
    for (size_t i = 0; i < count; i++) {
        size_t offset = i * 3;
        uint8_t crc = scd40_crc8(&buf[offset], 2);
        if (crc != buf[offset + 2]) {
            ESP_LOGW(TAG, "CRC mismatch at word %d: calc=0x%02X recv=0x%02X",
                     (int)i, crc, buf[offset + 2]);
            return ESP_ERR_INVALID_CRC;
        }
        words[i] = ((uint16_t)buf[offset] << 8) | buf[offset + 1];
    }

    return ESP_OK;
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t scd40_init(const scd40_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    s_port = config->i2c_port;
    s_addr = config->i2c_addr ? config->i2c_addr : SCD40_I2C_ADDR;

    /* Stop any existing measurement first */
    esp_err_t ret = scd40_write_cmd(SCD40_CMD_STOP_MEAS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Stop measurement cmd failed (may be OK if not running): %s",
                 esp_err_to_name(ret));
    }
    vTaskDelay(pdMS_TO_TICKS(500));  /* Wait for stop */

    /* Reinitialize sensor */
    ret = scd40_write_cmd(SCD40_CMD_REINIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reinit failed: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(30));

    /* Start periodic measurement */
    ret = scd40_start_measurement();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start measurement failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Initialized on I2C%d addr=0x%02X", s_port, s_addr);
    return ESP_OK;
}

esp_err_t scd40_start_measurement(void)
{
    return scd40_write_cmd(SCD40_CMD_START_MEAS);
}

esp_err_t scd40_stop_measurement(void)
{
    esp_err_t ret = scd40_write_cmd(SCD40_CMD_STOP_MEAS);
    vTaskDelay(pdMS_TO_TICKS(500));
    return ret;
}

esp_err_t scd40_is_data_ready(bool *ready)
{
    if (ready == NULL) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = scd40_write_cmd(SCD40_CMD_DATA_READY);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(1));

    uint16_t word;
    ret = scd40_read_words(&word, 1);
    if (ret != ESP_OK) return ret;

    /* Lower 11 bits != 0 means data ready */
    *ready = (word & 0x07FF) != 0;
    return ESP_OK;
}

esp_err_t scd40_read(scd40_data_t *data)
{
    if (data == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    data->valid = false;

    /* Check data ready */
    bool ready = false;
    esp_err_t ret = scd40_is_data_ready(&ready);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Data ready check failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (!ready) {
        ESP_LOGD(TAG, "Data not ready, waiting...");
        vTaskDelay(pdMS_TO_TICKS(SCD40_MEAS_INTERVAL_MS));

        ret = scd40_is_data_ready(&ready);
        if (ret != ESP_OK || !ready) {
            ESP_LOGW(TAG, "Data still not ready after wait");
            return ESP_ERR_TIMEOUT;
        }
    }

    /* Send read measurement command */
    ret = scd40_write_cmd(SCD40_CMD_READ_MEAS);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(1));

    /* Read 3 words: CO2, Temperature, Humidity */
    uint16_t words[3];
    ret = scd40_read_words(words, 3);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Read measurement failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Parse values */
    data->co2_ppm     = words[0];
    data->temperature = -45.0f + 175.0f * ((float)words[1] / 65536.0f);
    data->humidity    = 100.0f * ((float)words[2] / 65536.0f);

    /* Clamp humidity */
    if (data->humidity < 0.0f) data->humidity = 0.0f;
    if (data->humidity > 100.0f) data->humidity = 100.0f;

    /* Validate CO2 range */
    if (data->co2_ppm < 400 || data->co2_ppm > 5000) {
        ESP_LOGW(TAG, "CO₂ out of range: %u ppm", data->co2_ppm);
        return ESP_ERR_INVALID_RESPONSE;
    }

    data->valid = true;
    ESP_LOGD(TAG, "CO₂=%u ppm T=%.1f°C H=%.1f%%",
             data->co2_ppm, data->temperature, data->humidity);
    return ESP_OK;
}

esp_err_t scd40_set_temperature_offset(float offset_c)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* Convert to raw: offset × 65536 / 175 */
    uint16_t raw = (uint16_t)(offset_c * 65536.0f / 175.0f);
    return scd40_write_cmd_with_arg(SCD40_CMD_SET_TEMP_OFFSET, raw);
}

esp_err_t scd40_set_altitude(uint16_t altitude_m)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    return scd40_write_cmd_with_arg(SCD40_CMD_SET_ALTITUDE, altitude_m);
}

esp_err_t scd40_force_recalibration(uint16_t target_co2_ppm)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* Must stop measurement first */
    esp_err_t ret = scd40_stop_measurement();
    if (ret != ESP_OK) return ret;

    ret = scd40_write_cmd_with_arg(SCD40_CMD_FORCED_RECAL, target_co2_ppm);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(400));  /* Wait for recalibration */

    uint16_t correction;
    ret = scd40_read_words(&correction, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Recalibration response read failed");
        return ret;
    }

    if (correction == 0xFFFF) {
        ESP_LOGE(TAG, "Recalibration failed (sensor returned 0xFFFF)");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Recalibration correction: %d", (int16_t)(correction - 0x8000));

    /* Restart measurement */
    return scd40_start_measurement();
}

void scd40_deinit(void)
{
    if (s_initialized) {
        scd40_stop_measurement();
        s_initialized = false;
        ESP_LOGI(TAG, "Deinitialized");
    }
}
