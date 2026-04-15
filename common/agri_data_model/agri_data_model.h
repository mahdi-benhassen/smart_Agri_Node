/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * agri_data_model.h — Shared data structures and JSON codec API
 * Used by both Gateway and Field Node firmware.
 */

#ifndef AGRI_DATA_MODEL_H
#define AGRI_DATA_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Schema Version ────────────────────────────────────────────────── */
#define AGRI_SCHEMA_VER         2
#define AGRI_FW_VERSION         "1.4.2"
#define AGRI_DEVICE_ID_LEN      16
#define AGRI_FIELD_ID_LEN       16
#define AGRI_MAX_JSON_LEN       4096

/* ── Alarm Flag Bit Definitions ────────────────────────────────────── */
#define ALARM_FLAG_DHT22_FAULT      (1U << 0)
#define ALARM_FLAG_SHT40_FAULT      (1U << 1)
#define ALARM_FLAG_SOIL_FAULT       (1U << 2)
#define ALARM_FLAG_NPK_FAULT        (1U << 3)
#define ALARM_FLAG_RAIN_FAULT       (1U << 4)
#define ALARM_FLAG_BH1750_FAULT     (1U << 5)
#define ALARM_FLAG_SCD40_FAULT      (1U << 6)
#define ALARM_FLAG_ZIGBEE_TX_FAIL   (1U << 7)
#define ALARM_FLAG_LOW_BATTERY      (1U << 8)
#define ALARM_FLAG_OTA_FAIL         (1U << 9)
#define ALARM_FLAG_NVS_FAULT        (1U << 10)
#define ALARM_FLAG_ACTUATOR_FAULT   (1U << 11)

/* ── Location Data ─────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    float    latitude;
    float    longitude;
    char     field[AGRI_FIELD_ID_LEN];
} agri_location_t;

/* ── Sensor Data Structure ─────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  schema_ver;                /* AGRI_SCHEMA_VER */
    char     device_id[AGRI_DEVICE_ID_LEN];
    char     fw_ver[12];
    char     timestamp[28];             /* ISO-8601 UTC */
    agri_location_t location;

    /* Sensor readings */
    float    temp_c;                    /* Ambient temperature °C */
    float    humidity_pct;              /* Relative humidity % */
    float    soil_moist_pct;            /* Volumetric water content % */
    float    soil_temp_c;               /* Soil temperature °C */
    uint16_t npk_n;                     /* Nitrogen mg/L */
    uint16_t npk_p;                     /* Phosphorus mg/L */
    uint16_t npk_k;                     /* Potassium mg/L */
    float    rain_mm;                   /* Cumulative rainfall mm */
    uint32_t lux;                       /* Light intensity lux */
    uint16_t co2_ppm;                   /* CO₂ concentration ppm */

    /* Actuator states */
    bool     valve_open;                /* Irrigation valve state */
    uint8_t  pump_pct;                  /* Pump PWM duty 0–100 */
    uint8_t  fan_pct;                   /* Fan PWM duty 0–100 */
    uint8_t  led_r_pct;                 /* LED red channel 0–100 */
    uint8_t  led_b_pct;                 /* LED blue channel 0–100 */

    /* System health */
    uint16_t batt_mv;                   /* Battery voltage mV */
    int8_t   rssi_dbm;                  /* Zigbee RSSI dBm */
    uint8_t  lqi;                       /* Link quality indicator */
    uint32_t uptime_s;                  /* Uptime in seconds */
    uint16_t alarm_flags;               /* Bit-field alarm flags */
} agri_sensor_data_t;

/* ── Actuator Command Types ────────────────────────────────────────── */
typedef enum {
    AGRI_CMD_VALVE_SET      = 0x01,
    AGRI_CMD_PUMP_SET       = 0x02,
    AGRI_CMD_FAN_SET        = 0x03,
    AGRI_CMD_LED_R_SET      = 0x04,
    AGRI_CMD_LED_B_SET      = 0x05,
    AGRI_CMD_CONFIG_UPDATE  = 0x10,
    AGRI_CMD_OTA_TRIGGER    = 0x20,
    AGRI_CMD_REBOOT         = 0x30,
    AGRI_CMD_RAIN_RESET     = 0x40,
} agri_cmd_type_t;

/* ── Actuator Command Structure ────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t          schema_ver;
    char             device_id[AGRI_DEVICE_ID_LEN];
    agri_cmd_type_t  cmd_type;
    union {
        bool     valve_state;
        uint8_t  duty_pct;              /* pump, fan, led */
        struct {
            uint8_t  led_r_pct;
            uint8_t  led_b_pct;
        } led;
        char     ota_url[128];
        uint32_t config_value;
    } payload;
    uint32_t         cmd_id;            /* Unique command ID for ACK */
    char             timestamp[28];
} agri_cmd_t;

/* ── Device Status ─────────────────────────────────────────────────── */
typedef enum {
    AGRI_NODE_ONLINE  = 0,
    AGRI_NODE_OFFLINE = 1,
    AGRI_NODE_JOINING = 2,
    AGRI_NODE_OTA     = 3,
    AGRI_NODE_ERROR   = 4,
} agri_node_status_t;

typedef struct __attribute__((packed)) {
    char               device_id[AGRI_DEVICE_ID_LEN];
    agri_node_status_t status;
    char               fw_ver[12];
    uint16_t           batt_mv;
    int8_t             rssi_dbm;
    uint8_t            lqi;
    uint32_t           uptime_s;
    uint16_t           alarm_flags;
    uint16_t           zigbee_short_addr;
    char               last_seen[28];   /* ISO-8601 */
} agri_node_info_t;

/* ── Calibration Data (NVS) ────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    float   temp_offset;
    float   temp_gain;
    float   humidity_offset;
    float   humidity_gain;
    float   soil_offset;
    float   soil_gain;
    int16_t npk_n_offset;
    int16_t npk_p_offset;
    int16_t npk_k_offset;
} agri_calibration_t;

/* ── JSON Codec API ────────────────────────────────────────────────── */

/**
 * @brief Encode sensor data to JSON string.
 *
 * @param data  Pointer to sensor data struct
 * @param buf   Output buffer (must be >= AGRI_MAX_JSON_LEN)
 * @param len   Buffer length
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG or ESP_ERR_NO_MEM on failure
 */
esp_err_t agri_data_to_json(const agri_sensor_data_t *data, char *buf, size_t len);

/**
 * @brief Parse JSON telemetry string into sensor data struct.
 *
 * @param json  Null-terminated JSON string
 * @param data  Output struct
 * @return ESP_OK on success, ESP_FAIL on parse error
 */
esp_err_t agri_data_from_json(const char *json, agri_sensor_data_t *data);

/**
 * @brief Parse JSON command string into command struct.
 *
 * @param json  Null-terminated JSON string
 * @param cmd   Output command struct
 * @return ESP_OK on success, ESP_FAIL on parse error
 */
esp_err_t agri_cmd_from_json(const char *json, agri_cmd_t *cmd);

/**
 * @brief Encode command struct to JSON string.
 *
 * @param cmd  Pointer to command struct
 * @param buf  Output buffer
 * @param len  Buffer length
 * @return ESP_OK on success
 */
esp_err_t agri_cmd_to_json(const agri_cmd_t *cmd, char *buf, size_t len);

/**
 * @brief Encode node info to JSON string.
 *
 * @param info Pointer to node info struct
 * @param buf  Output buffer
 * @param len  Buffer length
 * @return ESP_OK on success
 */
esp_err_t agri_node_info_to_json(const agri_node_info_t *info, char *buf, size_t len);

/**
 * @brief Initialize data struct with safe defaults.
 *
 * @param data Pointer to sensor data struct
 */
void agri_data_init_defaults(agri_sensor_data_t *data);

/**
 * @brief Initialize calibration struct with unity/zero defaults.
 *
 * @param cal Pointer to calibration struct
 */
void agri_calibration_init_defaults(agri_calibration_t *cal);

/**
 * @brief Apply calibration offsets and gains to raw sensor data.
 *
 * @param data Raw sensor data (modified in place)
 * @param cal  Calibration coefficients
 */
void agri_data_apply_calibration(agri_sensor_data_t *data, const agri_calibration_t *cal);

#ifdef __cplusplus
}
#endif

#endif /* AGRI_DATA_MODEL_H */
