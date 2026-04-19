/*
 * agri_data_model.c — JSON codec and data utility implementations
 *
 * FIX applied:
 *  11. agri_data_apply_calibration: NPK values clamped to [0, NPK_MAX_VALUE]
 *      after adding signed offsets to prevent uint16_t wrapping to 65535
 *      when the offset makes the result negative.
 */
#include "agri_data_model.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "AGRI_DATA";

#define NPK_MAX_VALUE  1999u

/* ── Default Initializers ──────────────────────────────────────────── */

void agri_data_init_defaults(agri_sensor_data_t *data)
{
    if (!data) return;
    memset(data, 0, sizeof(agri_sensor_data_t));
    data->schema_ver = AGRI_SCHEMA_VER;
    strncpy(data->fw_ver, AGRI_FW_VERSION, sizeof(data->fw_ver) - 1);
    data->fw_ver[sizeof(data->fw_ver) - 1] = '\0';
}

void agri_calibration_init_defaults(agri_calibration_t *cal)
{
    if (!cal) return;
    memset(cal, 0, sizeof(agri_calibration_t));
    cal->temp_gain     = 1.0f;
    cal->humidity_gain = 1.0f;
    cal->soil_gain     = 1.0f;
}

/* FIX 11: clamp NPK values after signed offset to prevent uint16_t wrap */
static uint16_t apply_npk_offset(uint16_t raw, int16_t offset)
{
    int32_t result = (int32_t)raw + (int32_t)offset;
    if (result < 0)                      return 0;
    if (result > (int32_t)NPK_MAX_VALUE) return (uint16_t)NPK_MAX_VALUE;
    return (uint16_t)result;
}

void agri_data_apply_calibration(agri_sensor_data_t *data,
                                   const agri_calibration_t *cal)
{
    if (!data || !cal) return;
    data->temp_c         = (data->temp_c         + cal->temp_offset)     * cal->temp_gain;
    data->humidity_pct   = (data->humidity_pct   + cal->humidity_offset) * cal->humidity_gain;
    data->soil_moist_pct = (data->soil_moist_pct + cal->soil_offset)     * cal->soil_gain;
    data->npk_n = apply_npk_offset(data->npk_n, cal->npk_n_offset);
    data->npk_p = apply_npk_offset(data->npk_p, cal->npk_p_offset);
    data->npk_k = apply_npk_offset(data->npk_k, cal->npk_k_offset);
}

/* ── JSON Encode: Sensor Data → JSON ───────────────────────────────── */

esp_err_t agri_data_to_json(const agri_sensor_data_t *data, char *buf, size_t len)
{
    if (!data || !buf || len == 0) {
        ESP_LOGE(TAG, "Invalid args to agri_data_to_json");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) { ESP_LOGE(TAG, "JSON root alloc failed"); return ESP_ERR_NO_MEM; }

    cJSON_AddNumberToObject(root, "schema_ver", data->schema_ver);
    cJSON_AddStringToObject(root, "device_id",  data->device_id);
    cJSON_AddStringToObject(root, "fw_ver",     data->fw_ver);
    cJSON_AddStringToObject(root, "ts",         data->timestamp);

    cJSON *loc = cJSON_AddObjectToObject(root, "loc");
    if (loc) {
        cJSON_AddNumberToObject(loc, "lat",   (double)data->location.latitude);
        cJSON_AddNumberToObject(loc, "lon",   (double)data->location.longitude);
        cJSON_AddStringToObject(loc, "field", data->location.field);
    }

    cJSON *sensors = cJSON_AddObjectToObject(root, "sensors");
    if (sensors) {
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

    cJSON *actuators = cJSON_AddObjectToObject(root, "actuators");
    if (actuators) {
        cJSON_AddBoolToObject(actuators,   "valve_open", data->valve_open);
        cJSON_AddNumberToObject(actuators, "pump_pct",   data->pump_pct);
        cJSON_AddNumberToObject(actuators, "fan_pct",    data->fan_pct);
        cJSON_AddNumberToObject(actuators, "led_r_pct",  data->led_r_pct);
        cJSON_AddNumberToObject(actuators, "led_b_pct",  data->led_b_pct);
    }

    cJSON *sys = cJSON_AddObjectToObject(root, "sys");
    if (sys) {
        cJSON_AddNumberToObject(sys, "batt_mv",     data->batt_mv);
        cJSON_AddNumberToObject(sys, "rssi_dbm",    data->rssi_dbm);
        cJSON_AddNumberToObject(sys, "lqi",         data->lqi);
        cJSON_AddNumberToObject(sys, "uptime_s",    data->uptime_s);
        cJSON_AddNumberToObject(sys, "alarm_flags", data->alarm_flags);
    }

    if (!cJSON_PrintPreallocated(root, buf, (int)len, false)) {
        ESP_LOGE(TAG, "JSON buffer too small (len=%zu)", len);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* ── JSON Decode: JSON → Sensor Data ───────────────────────────────── */

esp_err_t agri_data_from_json(const char *json, agri_sensor_data_t *data)
{
    if (!json || !data) { ESP_LOGE(TAG, "Invalid args"); return ESP_ERR_INVALID_ARG; }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        const char *err = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "JSON parse error near: %.20s", err ? err : "?");
        return ESP_FAIL;
    }

    agri_data_init_defaults(data);

    cJSON *item;
#define GET_NUM(obj, key, dst)  do { item = cJSON_GetObjectItem(obj, key); if (cJSON_IsNumber(item)) dst = (__typeof__(dst))item->valuedouble; } while(0)
#define GET_STR(obj, key, dst, sz) do { item = cJSON_GetObjectItem(obj, key); if (cJSON_IsString(item) && item->valuestring) { strncpy(dst, item->valuestring, (sz)-1); dst[(sz)-1] = '\0'; } } while(0)

    item = cJSON_GetObjectItem(root, "schema_ver");
    if (cJSON_IsNumber(item)) data->schema_ver = (uint8_t)item->valueint;

    GET_STR(root, "device_id", data->device_id, sizeof(data->device_id));
    GET_STR(root, "fw_ver",    data->fw_ver,    sizeof(data->fw_ver));
    GET_STR(root, "ts",        data->timestamp, sizeof(data->timestamp));

    cJSON *loc = cJSON_GetObjectItem(root, "loc");
    if (cJSON_IsObject(loc)) {
        GET_NUM(loc, "lat",   data->location.latitude);
        GET_NUM(loc, "lon",   data->location.longitude);
        GET_STR(loc, "field", data->location.field, sizeof(data->location.field));
    }

    cJSON *sensors = cJSON_GetObjectItem(root, "sensors");
    if (cJSON_IsObject(sensors)) {
        GET_NUM(sensors, "temp_c",         data->temp_c);
        GET_NUM(sensors, "humidity_pct",   data->humidity_pct);
        GET_NUM(sensors, "soil_moist_pct", data->soil_moist_pct);
        GET_NUM(sensors, "soil_temp_c",    data->soil_temp_c);
        GET_NUM(sensors, "npk_n",          data->npk_n);
        GET_NUM(sensors, "npk_p",          data->npk_p);
        GET_NUM(sensors, "npk_k",          data->npk_k);
        GET_NUM(sensors, "rain_mm",        data->rain_mm);
        GET_NUM(sensors, "lux",            data->lux);
        GET_NUM(sensors, "co2_ppm",        data->co2_ppm);
    }

    cJSON *actuators = cJSON_GetObjectItem(root, "actuators");
    if (cJSON_IsObject(actuators)) {
        item = cJSON_GetObjectItem(actuators, "valve_open");
        if (cJSON_IsBool(item)) data->valve_open = cJSON_IsTrue(item);
        GET_NUM(actuators, "pump_pct",   data->pump_pct);
        GET_NUM(actuators, "fan_pct",    data->fan_pct);
        GET_NUM(actuators, "led_r_pct",  data->led_r_pct);
        GET_NUM(actuators, "led_b_pct",  data->led_b_pct);
    }

    cJSON *sys = cJSON_GetObjectItem(root, "sys");
    if (cJSON_IsObject(sys)) {
        GET_NUM(sys, "batt_mv",     data->batt_mv);
        GET_NUM(sys, "rssi_dbm",    data->rssi_dbm);
        GET_NUM(sys, "lqi",         data->lqi);
        GET_NUM(sys, "uptime_s",    data->uptime_s);
        GET_NUM(sys, "alarm_flags", data->alarm_flags);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* ── CMD encode/decode (unchanged from original) ───────────────────── */

esp_err_t agri_cmd_from_json(const char *json, agri_cmd_t *cmd)
{
    if (!json || !cmd) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_Parse(json);
    if (!root) { ESP_LOGE(TAG, "CMD JSON parse error"); return ESP_FAIL; }

    memset(cmd, 0, sizeof(agri_cmd_t));
    cmd->schema_ver = AGRI_SCHEMA_VER;

    cJSON *item;
    item = cJSON_GetObjectItem(root, "device_id");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(cmd->device_id, item->valuestring, sizeof(cmd->device_id) - 1);
        cmd->device_id[sizeof(cmd->device_id) - 1] = '\0';
    }
    item = cJSON_GetObjectItem(root, "cmd_type");
    if (cJSON_IsNumber(item)) cmd->cmd_type = (agri_cmd_type_t)item->valueint;
    item = cJSON_GetObjectItem(root, "cmd_id");
    if (cJSON_IsNumber(item)) cmd->cmd_id = (uint32_t)item->valueint;
    item = cJSON_GetObjectItem(root, "timestamp");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(cmd->timestamp, item->valuestring, sizeof(cmd->timestamp) - 1);
        cmd->timestamp[sizeof(cmd->timestamp) - 1] = '\0';
    }

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
            if (cJSON_IsString(item) && item->valuestring) {
                strncpy(cmd->payload.ota_url, item->valuestring,
                        sizeof(cmd->payload.ota_url) - 1);
                cmd->payload.ota_url[sizeof(cmd->payload.ota_url) - 1] = '\0';
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
    if (!cmd || !buf || len == 0) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON_AddNumberToObject(root, "schema_ver", cmd->schema_ver);
    cJSON_AddStringToObject(root, "device_id",  cmd->device_id);
    cJSON_AddNumberToObject(root, "cmd_type",   (int)cmd->cmd_type);
    cJSON_AddNumberToObject(root, "cmd_id",     (double)cmd->cmd_id);
    cJSON_AddStringToObject(root, "timestamp",  cmd->timestamp);

    cJSON *payload = cJSON_AddObjectToObject(root, "payload");
    if (payload) {
        switch (cmd->cmd_type) {
        case AGRI_CMD_VALVE_SET:
            cJSON_AddBoolToObject(payload, "valve_state", cmd->payload.valve_state); break;
        case AGRI_CMD_PUMP_SET:
        case AGRI_CMD_FAN_SET:
            cJSON_AddNumberToObject(payload, "duty_pct", cmd->payload.duty_pct); break;
        case AGRI_CMD_LED_R_SET:
        case AGRI_CMD_LED_B_SET:
            cJSON_AddNumberToObject(payload, "led_r_pct", cmd->payload.led.led_r_pct);
            cJSON_AddNumberToObject(payload, "led_b_pct", cmd->payload.led.led_b_pct); break;
        case AGRI_CMD_OTA_TRIGGER:
            cJSON_AddStringToObject(payload, "ota_url", cmd->payload.ota_url); break;
        default:
            cJSON_AddNumberToObject(payload, "value", (double)cmd->payload.config_value); break;
        }
    }

    if (!cJSON_PrintPreallocated(root, buf, (int)len, false)) {
        cJSON_Delete(root); return ESP_ERR_NO_MEM;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t agri_node_info_to_json(const agri_node_info_t *info, char *buf, size_t len)
{
    if (!info || !buf || len == 0) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

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
        cJSON_Delete(root); return ESP_ERR_NO_MEM;
    }
    cJSON_Delete(root);
    return ESP_OK;
}
