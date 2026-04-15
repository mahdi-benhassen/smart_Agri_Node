/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * agri_zigbee_clusters.h — Custom AGRI_CLUSTER (0xFF00) definitions
 * Manufacturer-specific Zigbee cluster for smart agriculture telemetry.
 */

#ifndef AGRI_ZIGBEE_CLUSTERS_H
#define AGRI_ZIGBEE_CLUSTERS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Cluster & Manufacturer IDs ────────────────────────────────────── */
#define AGRI_CLUSTER_ID             0xFF00
#define AGRI_MANUFACTURER_ID        0x1234
#define AGRI_ENDPOINT               10
#define AGRI_PROFILE_ID             0x0104  /* HA profile */

/* ── Custom Attribute IDs (AGRI_CLUSTER 0xFF00) ────────────────────── */
#define ATTR_SOIL_MOISTURE          0x0001  /* U16 — VWC × 100 (0–6000) */
#define ATTR_SOIL_TEMP              0x0002  /* S16 — Soil temp × 100 */
#define ATTR_NPK_NITROGEN           0x0003  /* U16 — N mg/L */
#define ATTR_NPK_PHOSPHORUS         0x0004  /* U16 — P mg/L */
#define ATTR_NPK_POTASSIUM          0x0005  /* U16 — K mg/L */
#define ATTR_RAIN_MM_X100           0x0006  /* U32 — Cumulative rain × 100 */
#define ATTR_LUX                    0x0007  /* U32 — Light intensity */
#define ATTR_CO2_PPM                0x0008  /* U16 — CO₂ ppm */
#define ATTR_BATT_MV                0x0009  /* U16 — Battery voltage mV */
#define ATTR_VALVE_STATE            0x000A  /* Bool — Valve open/close */
#define ATTR_PUMP_PCT               0x000B  /* U8  — Pump duty 0–100% */
#define ATTR_FAN_PCT                0x000C  /* U8  — Fan duty 0–100% */
#define ATTR_LED_R_PCT              0x000D  /* U8  — LED Red 0–100% */
#define ATTR_LED_B_PCT              0x000E  /* U8  — LED Blue 0–100% */
#define ATTR_ALARM_FLAGS            0x000F  /* U16 — Sensor fault flags */

#define AGRI_CLUSTER_ATTR_COUNT     15

/* ── Standard Cluster IDs Used ─────────────────────────────────────── */
#define ZB_CLUSTER_BASIC            0x0000
#define ZB_CLUSTER_POWER_CONFIG     0x0001
#define ZB_CLUSTER_IDENTIFY         0x0003
#define ZB_CLUSTER_ON_OFF           0x0006
#define ZB_CLUSTER_LEVEL_CONTROL    0x0008
#define ZB_CLUSTER_TIME             0x000A
#define ZB_CLUSTER_OTA_UPGRADE      0x0019
#define ZB_CLUSTER_TEMPERATURE      0x0402
#define ZB_CLUSTER_REL_HUMIDITY     0x0405

/* ── Zigbee ZCL Attribute Types (ESP-ZB SDK compatible) ────────────── */
#define ZB_ZCL_ATTR_TYPE_BOOL       0x10
#define ZB_ZCL_ATTR_TYPE_U8        0x20
#define ZB_ZCL_ATTR_TYPE_U16       0x21
#define ZB_ZCL_ATTR_TYPE_S16       0x29
#define ZB_ZCL_ATTR_TYPE_U32       0x23

/* ── Attribute Access Modes ────────────────────────────────────────── */
#define AGRI_ATTR_ACCESS_READ       0x01
#define AGRI_ATTR_ACCESS_WRITE      0x02
#define AGRI_ATTR_ACCESS_RW         (AGRI_ATTR_ACCESS_READ | AGRI_ATTR_ACCESS_WRITE)

/* ── Attribute Descriptor ──────────────────────────────────────────── */
typedef struct {
    uint16_t    attr_id;
    uint8_t     type;           /* ZCL attribute type */
    uint8_t     access;         /* Read / Write / RW */
    uint8_t     size;           /* Size in bytes */
    const char *name;           /* Human-readable name */
} agri_attr_desc_t;

/* ── Cluster Attribute Table ───────────────────────────────────────── */
extern const agri_attr_desc_t agri_cluster_attrs[AGRI_CLUSTER_ATTR_COUNT];

/* ── API Functions ─────────────────────────────────────────────────── */

/**
 * @brief Get attribute descriptor by attribute ID.
 *
 * @param attr_id Attribute ID (0x0001–0x000F)
 * @return Pointer to descriptor, or NULL if not found
 */
const agri_attr_desc_t *agri_cluster_get_attr_desc(uint16_t attr_id);

/**
 * @brief Get the size in bytes for a given ZCL attribute type.
 *
 * @param zcl_type ZCL type code
 * @return Size in bytes (0 if unknown)
 */
uint8_t agri_zcl_type_size(uint8_t zcl_type);

/**
 * @brief Validate that an attribute value is within acceptable range.
 *
 * @param attr_id   Attribute ID
 * @param value     Pointer to value data
 * @param value_len Length of value data
 * @return true if valid, false otherwise
 */
bool agri_cluster_validate_attr(uint16_t attr_id, const void *value, size_t value_len);

#ifdef __cplusplus
}
#endif

#endif /* AGRI_ZIGBEE_CLUSTERS_H */
