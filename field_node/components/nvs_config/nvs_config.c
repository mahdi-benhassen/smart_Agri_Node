/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * nvs_config.c — NVS persistent configuration implementation
 */

#include "nvs_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "NVS_CFG";
static nvs_handle_t s_nvs_handle = 0;
static bool s_initialized = false;

esp_err_t nvs_config_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated — erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS flash init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS open '%s' failed: %s", NVS_NAMESPACE, esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "NVS initialized (namespace: %s)", NVS_NAMESPACE);
    return ESP_OK;
}

esp_err_t nvs_config_load(void)
{
    if (!s_initialized) {
        esp_err_t ret = nvs_config_init();
        if (ret != ESP_OK) return ret;
    }
    ESP_LOGI(TAG, "Configuration loaded from NVS");
    return ESP_OK;
}

/* ── String ────────────────────────────────────────────────────────── */

esp_err_t nvs_config_get_str(const char *key, char *value, size_t max_len)
{
    if (!s_initialized || key == NULL || value == NULL) return ESP_ERR_INVALID_ARG;

    size_t required = max_len;
    esp_err_t ret = nvs_get_str(s_nvs_handle, key, value, &required);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        value[0] = '\0';
        ESP_LOGD(TAG, "Key '%s' not found — default empty", key);
        return ESP_OK;
    }
    return ret;
}

esp_err_t nvs_config_set_str(const char *key, const char *value)
{
    if (!s_initialized || key == NULL || value == NULL) return ESP_ERR_INVALID_ARG;
    return nvs_set_str(s_nvs_handle, key, value);
}

/* ── Integers ──────────────────────────────────────────────────────── */

esp_err_t nvs_config_get_u32(const char *key, uint32_t *value, uint32_t default_val)
{
    if (!s_initialized || key == NULL || value == NULL) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = nvs_get_u32(s_nvs_handle, key, value);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *value = default_val;
        ESP_LOGD(TAG, "Key '%s' not found — default %lu", key, (unsigned long)default_val);
        return ESP_OK;
    }
    return ret;
}

esp_err_t nvs_config_set_u32(const char *key, uint32_t value)
{
    if (!s_initialized || key == NULL) return ESP_ERR_INVALID_ARG;
    return nvs_set_u32(s_nvs_handle, key, value);
}

esp_err_t nvs_config_get_u16(const char *key, uint16_t *value, uint16_t default_val)
{
    if (!s_initialized || key == NULL || value == NULL) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = nvs_get_u16(s_nvs_handle, key, value);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *value = default_val;
        return ESP_OK;
    }
    return ret;
}

esp_err_t nvs_config_set_u16(const char *key, uint16_t value)
{
    if (!s_initialized || key == NULL) return ESP_ERR_INVALID_ARG;
    return nvs_set_u16(s_nvs_handle, key, value);
}

/* ── Float (stored as blob) ────────────────────────────────────────── */

esp_err_t nvs_config_get_float(const char *key, float *value, float default_val)
{
    if (!s_initialized || key == NULL || value == NULL) return ESP_ERR_INVALID_ARG;

    size_t size = sizeof(float);
    esp_err_t ret = nvs_get_blob(s_nvs_handle, key, value, &size);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *value = default_val;
        return ESP_OK;
    }
    return ret;
}

esp_err_t nvs_config_set_float(const char *key, float value)
{
    if (!s_initialized || key == NULL) return ESP_ERR_INVALID_ARG;
    return nvs_set_blob(s_nvs_handle, key, &value, sizeof(float));
}

/* ── Calibration ───────────────────────────────────────────────────── */

esp_err_t nvs_config_load_calibration(agri_calibration_t *cal)
{
    if (cal == NULL) return ESP_ERR_INVALID_ARG;

    agri_calibration_init_defaults(cal);

    nvs_config_get_float(NVS_KEY_TEMP_OFFSET, &cal->temp_offset, 0.0f);
    nvs_config_get_float(NVS_KEY_TEMP_GAIN,   &cal->temp_gain,   1.0f);
    nvs_config_get_float(NVS_KEY_HUM_OFFSET,  &cal->humidity_offset, 0.0f);
    nvs_config_get_float(NVS_KEY_HUM_GAIN,    &cal->humidity_gain,   1.0f);
    nvs_config_get_float(NVS_KEY_SOIL_OFFSET,  &cal->soil_offset, 0.0f);
    nvs_config_get_float(NVS_KEY_SOIL_GAIN,    &cal->soil_gain,   1.0f);

    ESP_LOGI(TAG, "Calibration loaded: temp_off=%.2f temp_gain=%.2f",
             cal->temp_offset, cal->temp_gain);
    return ESP_OK;
}

esp_err_t nvs_config_save_calibration(const agri_calibration_t *cal)
{
    if (cal == NULL) return ESP_ERR_INVALID_ARG;

    nvs_config_set_float(NVS_KEY_TEMP_OFFSET, cal->temp_offset);
    nvs_config_set_float(NVS_KEY_TEMP_GAIN,   cal->temp_gain);
    nvs_config_set_float(NVS_KEY_HUM_OFFSET,  cal->humidity_offset);
    nvs_config_set_float(NVS_KEY_HUM_GAIN,    cal->humidity_gain);
    nvs_config_set_float(NVS_KEY_SOIL_OFFSET,  cal->soil_offset);
    nvs_config_set_float(NVS_KEY_SOIL_GAIN,    cal->soil_gain);

    return nvs_config_commit();
}

/* ── Rain Counter ──────────────────────────────────────────────────── */

esp_err_t nvs_config_save_rain_count(uint32_t count)
{
    return nvs_config_set_u32(NVS_KEY_RAIN_COUNT, count);
}

esp_err_t nvs_config_load_rain_count(uint32_t *count)
{
    return nvs_config_get_u32(NVS_KEY_RAIN_COUNT, count, 0);
}

/* ── Commit ────────────────────────────────────────────────────────── */

esp_err_t nvs_config_commit(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    return nvs_commit(s_nvs_handle);
}
