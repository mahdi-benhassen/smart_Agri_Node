/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * nvs_config.h — NVS persistent configuration for field node
 */

#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <stdint.h>
#include "esp_err.h"
#include "agri_data_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NVS_NAMESPACE           "agri_config"
#define NVS_KEY_DEVICE_ID       "device_id"
#define NVS_KEY_FARM_ID         "farm_id"
#define NVS_KEY_FIELD_ID        "field_id"
#define NVS_KEY_SLEEP_SEC       "sleep_sec"
#define NVS_KEY_LATITUDE        "latitude"
#define NVS_KEY_LONGITUDE       "longitude"
#define NVS_KEY_RAIN_COUNT      "rain_count"
#define NVS_KEY_TEMP_OFFSET     "temp_off"
#define NVS_KEY_TEMP_GAIN       "temp_gain"
#define NVS_KEY_HUM_OFFSET      "hum_off"
#define NVS_KEY_HUM_GAIN        "hum_gain"
#define NVS_KEY_SOIL_OFFSET     "soil_off"
#define NVS_KEY_SOIL_GAIN       "soil_gain"
#define NVS_KEY_SOIL_CAL_DRY    "soil_dry"
#define NVS_KEY_SOIL_CAL_WET    "soil_wet"
#define NVS_KEY_FAN_KP          "fan_kp"
#define NVS_KEY_FAN_KI          "fan_ki"
#define NVS_KEY_FAN_KD          "fan_kd"
#define NVS_KEY_FAN_SETPOINT    "fan_sp"

esp_err_t nvs_config_init(void);
esp_err_t nvs_config_load(void);

/* String getters/setters */
esp_err_t nvs_config_get_str(const char *key, char *value, size_t max_len);
esp_err_t nvs_config_set_str(const char *key, const char *value);

/* Integer getters/setters */
esp_err_t nvs_config_get_u32(const char *key, uint32_t *value, uint32_t default_val);
esp_err_t nvs_config_set_u32(const char *key, uint32_t value);
esp_err_t nvs_config_get_u16(const char *key, uint16_t *value, uint16_t default_val);
esp_err_t nvs_config_set_u16(const char *key, uint16_t value);

/* Float getters/setters */
esp_err_t nvs_config_get_float(const char *key, float *value, float default_val);
esp_err_t nvs_config_set_float(const char *key, float value);

/* Calibration struct load/save */
esp_err_t nvs_config_load_calibration(agri_calibration_t *cal);
esp_err_t nvs_config_save_calibration(const agri_calibration_t *cal);

/* Rain counter persistence */
esp_err_t nvs_config_save_rain_count(uint32_t count);
esp_err_t nvs_config_load_rain_count(uint32_t *count);

/* Commit changes */
esp_err_t nvs_config_commit(void);

#ifdef __cplusplus
}
#endif

#endif /* NVS_CONFIG_H */
