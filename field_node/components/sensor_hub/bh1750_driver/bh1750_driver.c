/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * bh1750_driver.c — BH1750 I2C ambient light sensor driver
 */

#include "bh1750_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BH1750";

static i2c_port_t s_port = I2C_NUM_0;
static uint8_t    s_addr = BH1750_I2C_ADDR_LOW;
static bool       s_initialized = false;

#define I2C_TIMEOUT_MS  100

/* ── Internal: Write single command byte ───────────────────────────── */

static esp_err_t bh1750_write_cmd(uint8_t cmd)
{
    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    if (handle == NULL) return ESP_ERR_NO_MEM;

    i2c_master_start(handle);
    i2c_master_write_byte(handle, (s_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(handle, cmd, true);
    i2c_master_stop(handle);

    esp_err_t ret = i2c_master_cmd_begin(s_port, handle, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(handle);
    return ret;
}

/* ── Internal: Read 2 bytes ────────────────────────────────────────── */

static esp_err_t bh1750_read_raw(uint16_t *raw)
{
    uint8_t buf[2];

    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    if (handle == NULL) return ESP_ERR_NO_MEM;

    i2c_master_start(handle);
    i2c_master_write_byte(handle, (s_addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(handle, &buf[0], I2C_MASTER_ACK);
    i2c_master_read_byte(handle, &buf[1], I2C_MASTER_NACK);
    i2c_master_stop(handle);

    esp_err_t ret = i2c_master_cmd_begin(s_port, handle, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(handle);

    if (ret == ESP_OK) {
        *raw = ((uint16_t)buf[0] << 8) | buf[1];
    }
    return ret;
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t bh1750_init(const bh1750_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    s_port = config->i2c_port;
    s_addr = config->i2c_addr ? config->i2c_addr : BH1750_I2C_ADDR_LOW;

    /* Power on */
    esp_err_t ret = bh1750_write_cmd(BH1750_CMD_POWER_ON);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Power-on failed: %s (addr=0x%02X)", esp_err_to_name(ret), s_addr);
        return ret;
    }

    /* Reset data register */
    ret = bh1750_write_cmd(BH1750_CMD_RESET);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Reset failed: %s", esp_err_to_name(ret));
        /* Non-fatal, continue */
    }

    /* Set continuous high-resolution mode */
    ret = bh1750_write_cmd(BH1750_CMD_CONT_H_RES);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mode set failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Initialized on I2C%d addr=0x%02X", s_port, s_addr);
    return ESP_OK;
}

esp_err_t bh1750_read(bh1750_data_t *data)
{
    if (data == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    data->valid = false;

    /* Wait for measurement (120ms typical for high-res mode) */
    vTaskDelay(pdMS_TO_TICKS(BH1750_MEAS_TIME_MS));

    uint16_t raw = 0;
    esp_err_t ret = bh1750_read_raw(&raw);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Convert: lux = raw / 1.2 */
    data->lux = (uint32_t)((float)raw / 1.2f);
    data->valid = true;

    ESP_LOGD(TAG, "Lux=%lu (raw=%u)", (unsigned long)data->lux, raw);
    return ESP_OK;
}

esp_err_t bh1750_set_mode(uint8_t mode_cmd)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    return bh1750_write_cmd(mode_cmd);
}

esp_err_t bh1750_power_down(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    return bh1750_write_cmd(BH1750_CMD_POWER_OFF);
}

void bh1750_deinit(void)
{
    if (s_initialized) {
        bh1750_write_cmd(BH1750_CMD_POWER_OFF);
        s_initialized = false;
        ESP_LOGI(TAG, "Deinitialized");
    }
}
