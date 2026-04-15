/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * nvs_config.h — Gateway NVS persistent configuration
 */

#ifndef GW_NVS_CONFIG_H
#define GW_NVS_CONFIG_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GW_NVS_NAMESPACE       "agri_config"

/* Keys */
#define GW_NVS_WIFI_SSID       "wifi_ssid"
#define GW_NVS_WIFI_PASS       "wifi_pass"
#define GW_NVS_MQTT_URI        "mqtt_uri"
#define GW_NVS_MQTT_USER       "mqtt_user"
#define GW_NVS_MQTT_PASS       "mqtt_pass"
#define GW_NVS_FARM_ID         "farm_id"
#define GW_NVS_DEVICE_ID       "device_id"
#define GW_NVS_OTA_URL         "ota_url"
#define GW_NVS_API_TOKEN       "api_token"
#define GW_NVS_NTP_SERVER      "ntp_server"

esp_err_t gw_nvs_config_init(void);
esp_err_t gw_nvs_config_get_str(const char *key, char *buf, size_t max_len, const char *def);
esp_err_t gw_nvs_config_set_str(const char *key, const char *value);
esp_err_t gw_nvs_config_get_u32(const char *key, uint32_t *val, uint32_t def);
esp_err_t gw_nvs_config_set_u32(const char *key, uint32_t value);
esp_err_t gw_nvs_config_commit(void);

#ifdef __cplusplus
}
#endif

#endif /* GW_NVS_CONFIG_H */
