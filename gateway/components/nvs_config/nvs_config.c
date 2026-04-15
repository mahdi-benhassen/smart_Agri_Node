/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * nvs_config.c — Gateway NVS configuration implementation
 */

#include "nvs_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "GW_NVS";
static nvs_handle_t s_handle = 0;
static bool s_initialized = false;

esp_err_t gw_nvs_config_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS partition...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "NVS flash init failed");

    ret = nvs_open(GW_NVS_NAMESPACE, NVS_READWRITE, &s_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "NVS open failed");

    s_initialized = true;
    ESP_LOGI(TAG, "NVS initialized");
    return ESP_OK;
}

esp_err_t gw_nvs_config_get_str(const char *key, char *buf, size_t max_len, const char *def)
{
    if (!s_initialized || !key || !buf) return ESP_ERR_INVALID_ARG;
    size_t len = max_len;
    esp_err_t ret = nvs_get_str(s_handle, key, buf, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        if (def) strncpy(buf, def, max_len - 1);
        else buf[0] = '\0';
        return ESP_OK;
    }
    return ret;
}

esp_err_t gw_nvs_config_set_str(const char *key, const char *value)
{
    if (!s_initialized || !key || !value) return ESP_ERR_INVALID_ARG;
    return nvs_set_str(s_handle, key, value);
}

esp_err_t gw_nvs_config_get_u32(const char *key, uint32_t *val, uint32_t def)
{
    if (!s_initialized || !key || !val) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = nvs_get_u32(s_handle, key, val);
    if (ret == ESP_ERR_NVS_NOT_FOUND) { *val = def; return ESP_OK; }
    return ret;
}

esp_err_t gw_nvs_config_set_u32(const char *key, uint32_t value)
{
    if (!s_initialized || !key) return ESP_ERR_INVALID_ARG;
    return nvs_set_u32(s_handle, key, value);
}

esp_err_t gw_nvs_config_commit(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    return nvs_commit(s_handle);
}
