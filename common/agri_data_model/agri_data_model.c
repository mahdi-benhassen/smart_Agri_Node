/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * agri_data_model.c — JSON codec and data utility implementations
 */

#include "agri_data_model.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "AGRI_DATA";

/* ── Default Initializers ──────────────────────────────────────────── */

void agri_data_init_defaults(agri_sensor_data_t *data)
{
    if (data == NULL) {
        return;
    }
    memset(data, 0, sizeof(agri_sensor_data_t));
    data->schema_ver = AGRI_SCHEMA_VER;
    strncpy(data->fw_ver, AGRI_FW_VERSION, sizeof(data->fw_ver) - 1);
}

void agri_calibration_init_defaults(agri_calibration_t *cal)
{
    if (cal == NULL) {
        return;
    }
    memset(cal, 0, sizeof(agri_calibration_t));
    cal->temp_gain     = 1.0f;
    cal->humidity_gain  = 1.0f;
    cal->soil_gain      = 1.0f;
    /* Offsets default to 0, NPK offsets default to 0 */
}

void agri_data_apply_calibration(agri_sensor_data_t *data, const agri_calibration_t *cal)
{
    if (data == NULL || cal == NULL) {
        return;
    }
    data->temp_c        = (data->temp_c + cal->temp_offset) * cal->temp_gain;
    data->humidity_pct  = (data->humidity_pct + cal->humidity_offset) * cal->humidity_gain;
    data->soil_moist_pct = (data->soil_moist_pct + cal->soil_offset) * cal->soil_gain;
    data->npk_n         = (uint16_t)((int32_t)data->npk_n + cal->npk_n_offset);
    data->npk_p         = (uint16_t)((int32_t)data->npk_p + cal->npk_p_offset);
    data->npk_k         = (uint16_t)((int32_t)data->npk_k + cal->npk_k_offset);
}

/* ── JSON Encode: Sensor Data → JSON ───────────────────────────────── */

esp_err_t agri_data_to_json(const agri_sensor_data_t *data, char *buf, size_t len)
{
    if (data == NULL || buf == NULL || len == 0) {
        ESP_LOGE(TAG, "Invalid args to agri_data_to_json");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON root object");
        return ESP_ERR_NO_MEM;
    }

    /* Top-level fields */
    cJSON_AddNumberToObject(root, "schema_ver", data->schema_ver);
    cJSON_AddStringToObject(root, "device_id",  data->device_id);
    cJSON_AddStringToObject(root, "fw_ver",     data->fw_ver);
    cJSON_AddStringToObject(root, "ts",         data->timestamp);

    /* Location */
    cJSON *loc = cJSON_AddObjectToObject(root, "loc");
    if (loc != NULL) {
        cJSON_AddNumberToObject(loc, "lat",   (double)data->location.latitude);
        cJSON_AddNumberToObject(loc, "lon",   (double)data->location.longitude);
        cJSON_AddStringToObject(loc, "field", data->location.field);
    }

    /* Sensors */
    cJSON *sensors = cJSON_AddObjectToObject(root, "sensors");
    if (sensors != NULL) {
        cJSON_AddNumberToObject(sensors, "temp_c",         (double)data->temp_c);
        cJSON_AddNumberToObject(sensors, "humidity_pct",   (double)data->humidity_pct);
        cJSON_AddNumberToObject(sensors, "soil_moist_pct", (double)data->soil_moist_pct);
        cJSON_AddNumberToObject(sensors, "soil_temp_c",    (double)data->soil_temp_c);
        cJSON_AddNumberToObject(sensors, "npk_n",          data->npk_n);
        cJSON_AddNumberToObject(sensors, "npk_p",          data->npk_p);
        cJSON_AddNumberToObject(sensors, "npk_k",          data->npk_k);
        cJSON_AddNumberToObject(sensors, "rain_mm",        (double)data->rain_mm);
        cJSON_AddNumberToObject(sensors, "lux",            data->lux);
        cJSON_AddNumberToObject(sensors, "co2_ppm",        data->co2_ppm);
    }

    /* Actuators */
    cJSON *actuators = cJSON_AddObjectToObject(root, "actuators");
    if (actuators != NULL) {
        cJSON_AddBoolToObject(actuators,   "valve_open", data->valve_open);
        cJSON_AddNumberToObject(actuators, "pump_pct",   data->pump_pct);
        cJSON_AddNumberToObject(actuators, "fan_pct",    data->fan_pct);
        cJSON_AddNumberToObject(actuators, "led_r_pct",  data->led_r_pct);
        cJSON_AddNumberToObject(actuators, "led_b_pct",  data->led_b_pct);
    }

    /* System health */
    cJSON *sys = cJSON_AddObjectToObject(root, "sys");
    if (sys != NULL) {
        cJSON_AddNumberToObject(sys, "batt_mv",     data->batt_mv);
        cJSON_AddNumberToObject(sys, "rssi_dbm",    data->rssi_dbm);
        cJSON_AddNumberToObject(sys, "lqi",         data->lqi);
        cJSON_AddNumberToObject(sys, "uptime_s",    data->uptime_s);
        cJSON_AddNumberToObject(sys, "alarm_flags", data->alarm_flags);
    }

    /* Print to buffer */
    if (!cJSON_PrintPreallocated(root, buf, (int)len, false)) {
        ESP_LOGE(TAG, "JSON buffer too small (need > %d)", (int)len);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* ── JSON Decode: JSON → Sensor Data ───────────────────────────────── */

esp_err_t agri_data_from_json(const char *json, agri_sensor_data_t *data)
{
    if (json == NULL || data == NULL) {
        ESP_LOGE(TAG, "Invalid args to agri_data_from_json");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        const char *err = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "JSON parse error near: %.20s", err ? err : "unknown");
        return ESP_FAIL;
    }

    agri_data_init_defaults(data);

    /* Top-level */
    cJSON *item = cJSON_GetObjectItem(root, "schema_ver");
    if (cJSON_IsNumber(item)) {
        data->schema_ver = (uint8_t)item->valueint;
    }

    item = cJSON_GetObjectItem(root, "device_id");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strncpy(data->device_id, item->valuestring, sizeof(data->device_id) - 1);
    }

    item = cJSON_GetObjectItem(root, "fw_ver");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strncpy(data->fw_ver, item->valuestring, sizeof(data->fw_ver) - 1);
    }

    item = cJSON_GetObjectItem(root, "ts");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strncpy(data->timestamp, item->valuestring, sizeof(data->timestamp) - 1);
    }

    /* Location */
    cJSON *loc = cJSON_GetObjectItem(root, "loc");
    if (cJSON_IsObject(loc)) {
        item = cJSON_GetObjectItem(loc, "lat");
        if (cJSON_IsNumber(item)) data->location.latitude = (float)item->valuedouble;
        item = cJSON_GetObjectItem(loc, "lon");
        if (cJSON_IsNumber(item)) data->location.longitude = (float)item->valuedouble;
        item = cJSON_GetObjectItem(loc, "field");
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            strncpy(data->location.field, item->valuestring, sizeof(data->location.field) - 1);
        }
    }

    /* Sensors */
    cJSON *sensors = cJSON_GetObjectItem(root, "sensors");
    if (cJSON_IsObject(sensors)) {
        item = cJSON_GetObjectItem(sensors, "temp_c");
        if (cJSON_IsNumber(item)) data->temp_c = (float)item->valuedouble;
        item = cJSON_GetObjectItem(sensors, "humidity_pct");
        if (cJSON_IsNumber(item)) data->humidity_pct = (float)item->valuedouble;
        item = cJSON_GetObjectItem(sensors, "soil_moist_pct");
        if (cJSON_IsNumber(item)) data->soil_moist_pct = (float)item->valuedouble;
        item = cJSON_GetObjectItem(sensors, "soil_temp_c");
        if (cJSON_IsNumber(item)) data->soil_temp_c = (float)item->valuedouble;
        item = cJSON_GetObjectItem(sensors, "npk_n");
        if (cJSON_IsNumber(item)) data->npk_n = (uint16_t)item->valueint;
        item = cJSON_GetObjectItem(sensors, "npk_p");
        if (cJSON_IsNumber(item)) data->npk_p = (uint16_t)item->valueint;
        item = cJSON_GetObjectItem(sensors, "npk_k");
        if (cJSON_IsNumber(item)) data->npk_k = (uint16_t)item->valueint;
        item = cJSON_GetObjectItem(sensors, "rain_mm");
        if (cJSON_IsNumber(item)) data->rain_mm = (float)item->valuedouble;
        item = cJSON_GetObjectItem(sensors, "lux");
        if (cJSON_IsNumber(item)) data->lux = (uint32_t)item->valueint;
        item = cJSON_GetObjectItem(sensors, "co2_ppm");
        if (cJSON_IsNumber(item)) data->co2_ppm = (uint16_t)item->valueint;
    }

    /* Actuators */
    cJSON *actuators = cJSON_GetObjectItem(root, "actuators");
    if (cJSON_IsObject(actuators)) {
        item = cJSON_GetObjectItem(actuators, "valve_open");
        if (cJSON_IsBool(item)) data->valve_open = cJSON_IsTrue(item);
        item = cJSON_GetObjectItem(actuators, "pump_pct");
        if (cJSON_IsNumber(item)) data->pump_pct = (uint8_t)item->valueint;
        item = cJSON_GetObjectItem(actuators, "fan_pct");
        if (cJSON_IsNumber(item)) data->fan_pct = (uint8_t)item->valueint;
        item = cJSON_GetObjectItem(actuators, "led_r_pct");
        if (cJSON_IsNumber(item)) data->led_r_pct = (uint8_t)item->valueint;
        item = cJSON_GetObjectItem(actuators, "led_b_pct");
        if (cJSON_IsNumber(item)) data->led_b_pct = (uint8_t)item->valueint;
    }

    /* System */
    cJSON *sys = cJSON_GetObjectItem(root, "sys");
    if (cJSON_IsObject(sys)) {
        item = cJSON_GetObjectItem(sys, "batt_mv");
        if (cJSON_IsNumber(item)) data->batt_mv = (uint16_t)item->valueint;
        item = cJSON_GetObjectItem(sys, "rssi_dbm");
        if (cJSON_IsNumber(item)) data->rssi_dbm = (int8_t)item->valueint;
        item = cJSON_GetObjectItem(sys, "lqi");
        if (cJSON_IsNumber(item)) data->lqi = (uint8_t)item->valueint;
        item = cJSON_GetObjectItem(sys, "uptime_s");
        if (cJSON_IsNumber(item)) data->uptime_s = (uint32_t)item->valueint;
        item = cJSON_GetObjectItem(sys, "alarm_flags");
        if (cJSON_IsNumber(item)) data->alarm_flags = (uint16_t)item->valueint;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* ── JSON Encode/Decode: Command ───────────────────────────────────── */

esp_err_t agri_cmd_from_json(const char *json, agri_cmd_t *cmd)
{
    if (json == NULL || cmd == NULL) {
        ESP_LOGE(TAG, "Invalid args to agri_cmd_from_json");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "CMD JSON parse error");
        return ESP_FAIL;
    }

    memset(cmd, 0, sizeof(agri_cmd_t));
    cmd->schema_ver = AGRI_SCHEMA_VER;

    cJSON *item = cJSON_GetObjectItem(root, "device_id");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strncpy(cmd->device_id, item->valuestring, sizeof(cmd->device_id) - 1);
    }

    item = cJSON_GetObjectItem(root, "cmd_type");
    if (cJSON_IsNumber(item)) {
        cmd->cmd_type = (agri_cmd_type_t)item->valueint;
    }

    item = cJSON_GetObjectItem(root, "cmd_id");
    if (cJSON_IsNumber(item)) {
        cmd->cmd_id = (uint32_t)item->valueint;
    }

    item = cJSON_GetObjectItem(root, "timestamp");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strncpy(cmd->timestamp, item->valuestring, sizeof(cmd->timestamp) - 1);
    }

    /* Parse payload based on command type */
    cJSON *payload = cJSON_GetObjectItem(root, "payload");
    if (cJSON_IsObject(payload)) {
        switch (cmd->cmd_type) {
        case AGRI_CMD_VALVE_SET:
            item = cJSON_GetObjectItem(payload, "valve_state");
            if (cJSON_IsBool(item)) cmd->payload.valve_state = cJSON_IsTrue(item);
            break;

        case AGRI_CMD_PUMP_SET:
        case AGRI_CMD_FAN_SET:
            item = cJSON_GetObjectItem(payload, "duty_pct");
            if (cJSON_IsNumber(item)) cmd->payload.duty_pct = (uint8_t)item->valueint;
            break;

        case AGRI_CMD_LED_R_SET:
        case AGRI_CMD_LED_B_SET:
            item = cJSON_GetObjectItem(payload, "led_r_pct");
            if (cJSON_IsNumber(item)) cmd->payload.led.led_r_pct = (uint8_t)item->valueint;
            item = cJSON_GetObjectItem(payload, "led_b_pct");
            if (cJSON_IsNumber(item)) cmd->payload.led.led_b_pct = (uint8_t)item->valueint;
            break;

        case AGRI_CMD_OTA_TRIGGER:
            item = cJSON_GetObjectItem(payload, "ota_url");
            if (cJSON_IsString(item) && item->valuestring != NULL) {
                strncpy(cmd->payload.ota_url, item->valuestring,
                        sizeof(cmd->payload.ota_url) - 1);
            }
            break;

        default:
            item = cJSON_GetObjectItem(payload, "value");
            if (cJSON_IsNumber(item)) cmd->payload.config_value = (uint32_t)item->valueint;
            break;
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t agri_cmd_to_json(const agri_cmd_t *cmd, char *buf, size_t len)
{
    if (cmd == NULL || buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(root, "schema_ver", cmd->schema_ver);
    cJSON_AddStringToObject(root, "device_id",  cmd->device_id);
    cJSON_AddNumberToObject(root, "cmd_type",   (int)cmd->cmd_type);
    cJSON_AddNumberToObject(root, "cmd_id",     (double)cmd->cmd_id);
    cJSON_AddStringToObject(root, "timestamp",  cmd->timestamp);

    cJSON *payload = cJSON_AddObjectToObject(root, "payload");
    if (payload != NULL) {
        switch (cmd->cmd_type) {
        case AGRI_CMD_VALVE_SET:
            cJSON_AddBoolToObject(payload, "valve_state", cmd->payload.valve_state);
            break;
        case AGRI_CMD_PUMP_SET:
        case AGRI_CMD_FAN_SET:
            cJSON_AddNumberToObject(payload, "duty_pct", cmd->payload.duty_pct);
            break;
        case AGRI_CMD_LED_R_SET:
        case AGRI_CMD_LED_B_SET:
            cJSON_AddNumberToObject(payload, "led_r_pct", cmd->payload.led.led_r_pct);
            cJSON_AddNumberToObject(payload, "led_b_pct", cmd->payload.led.led_b_pct);
            break;
        case AGRI_CMD_OTA_TRIGGER:
            cJSON_AddStringToObject(payload, "ota_url", cmd->payload.ota_url);
            break;
        default:
            cJSON_AddNumberToObject(payload, "value", (double)cmd->payload.config_value);
            break;
        }
    }

    if (!cJSON_PrintPreallocated(root, buf, (int)len, false)) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* ── JSON Encode: Node Info ────────────────────────────────────────── */

esp_err_t agri_node_info_to_json(const agri_node_info_t *info, char *buf, size_t len)
{
    if (info == NULL || buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "device_id",   info->device_id);
    cJSON_AddNumberToObject(root, "status",      (int)info->status);
    cJSON_AddStringToObject(root, "fw_ver",      info->fw_ver);
    cJSON_AddNumberToObject(root, "batt_mv",     info->batt_mv);
    cJSON_AddNumberToObject(root, "rssi_dbm",    info->rssi_dbm);
    cJSON_AddNumberToObject(root, "lqi",         info->lqi);
    cJSON_AddNumberToObject(root, "uptime_s",    (double)info->uptime_s);
    cJSON_AddNumberToObject(root, "alarm_flags", info->alarm_flags);
    cJSON_AddNumberToObject(root, "zigbee_addr", info->zigbee_short_addr);
    cJSON_AddStringToObject(root, "last_seen",   info->last_seen);

    if (!cJSON_PrintPreallocated(root, buf, (int)len, false)) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_Delete(root);
    return ESP_OK;
}
