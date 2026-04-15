/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * agri_zigbee_clusters.c — Custom AGRI_CLUSTER attribute table and helpers
 */

#include "agri_zigbee_clusters.h"
#include <stddef.h>
#include <string.h>

/* ── Cluster Attribute Table ───────────────────────────────────────── */

const agri_attr_desc_t agri_cluster_attrs[AGRI_CLUSTER_ATTR_COUNT] = {
    { ATTR_SOIL_MOISTURE,  ZB_ZCL_ATTR_TYPE_U16,  AGRI_ATTR_ACCESS_READ, 2, "soil_moisture_pct" },
    { ATTR_SOIL_TEMP,      ZB_ZCL_ATTR_TYPE_S16,  AGRI_ATTR_ACCESS_READ, 2, "soil_temp_c"       },
    { ATTR_NPK_NITROGEN,   ZB_ZCL_ATTR_TYPE_U16,  AGRI_ATTR_ACCESS_READ, 2, "npk_nitrogen"      },
    { ATTR_NPK_PHOSPHORUS, ZB_ZCL_ATTR_TYPE_U16,  AGRI_ATTR_ACCESS_READ, 2, "npk_phosphorus"    },
    { ATTR_NPK_POTASSIUM,  ZB_ZCL_ATTR_TYPE_U16,  AGRI_ATTR_ACCESS_READ, 2, "npk_potassium"     },
    { ATTR_RAIN_MM_X100,   ZB_ZCL_ATTR_TYPE_U32,  AGRI_ATTR_ACCESS_READ, 4, "rain_mm_x100"      },
    { ATTR_LUX,            ZB_ZCL_ATTR_TYPE_U32,  AGRI_ATTR_ACCESS_READ, 4, "lux"               },
    { ATTR_CO2_PPM,        ZB_ZCL_ATTR_TYPE_U16,  AGRI_ATTR_ACCESS_READ, 2, "co2_ppm"           },
    { ATTR_BATT_MV,        ZB_ZCL_ATTR_TYPE_U16,  AGRI_ATTR_ACCESS_READ, 2, "batt_mv"           },
    { ATTR_VALVE_STATE,    ZB_ZCL_ATTR_TYPE_BOOL,  AGRI_ATTR_ACCESS_RW,  1, "valve_state"       },
    { ATTR_PUMP_PCT,       ZB_ZCL_ATTR_TYPE_U8,   AGRI_ATTR_ACCESS_RW,  1, "pump_pct"          },
    { ATTR_FAN_PCT,        ZB_ZCL_ATTR_TYPE_U8,   AGRI_ATTR_ACCESS_RW,  1, "fan_pct"           },
    { ATTR_LED_R_PCT,      ZB_ZCL_ATTR_TYPE_U8,   AGRI_ATTR_ACCESS_RW,  1, "led_r_pct"         },
    { ATTR_LED_B_PCT,      ZB_ZCL_ATTR_TYPE_U8,   AGRI_ATTR_ACCESS_RW,  1, "led_b_pct"         },
    { ATTR_ALARM_FLAGS,    ZB_ZCL_ATTR_TYPE_U16,  AGRI_ATTR_ACCESS_READ, 2, "alarm_flags"       },
};

/* ── Lookup by Attribute ID ────────────────────────────────────────── */

const agri_attr_desc_t *agri_cluster_get_attr_desc(uint16_t attr_id)
{
    for (int i = 0; i < AGRI_CLUSTER_ATTR_COUNT; i++) {
        if (agri_cluster_attrs[i].attr_id == attr_id) {
            return &agri_cluster_attrs[i];
        }
    }
    return NULL;
}

/* ── ZCL Type Size ─────────────────────────────────────────────────── */

uint8_t agri_zcl_type_size(uint8_t zcl_type)
{
    switch (zcl_type) {
    case ZB_ZCL_ATTR_TYPE_BOOL: return 1;
    case ZB_ZCL_ATTR_TYPE_U8:  return 1;
    case ZB_ZCL_ATTR_TYPE_U16: return 2;
    case ZB_ZCL_ATTR_TYPE_S16: return 2;
    case ZB_ZCL_ATTR_TYPE_U32: return 4;
    default:                    return 0;
    }
}

/* ── Attribute Validation ──────────────────────────────────────────── */

bool agri_cluster_validate_attr(uint16_t attr_id, const void *value, size_t value_len)
{
    if (value == NULL || value_len == 0) {
        return false;
    }

    const agri_attr_desc_t *desc = agri_cluster_get_attr_desc(attr_id);
    if (desc == NULL) {
        return false;
    }

    if (value_len < desc->size) {
        return false;
    }

    /* Range validation for specific attributes */
    switch (attr_id) {
    case ATTR_SOIL_MOISTURE: {
        uint16_t v;
        memcpy(&v, value, sizeof(v));
        return (v <= 6000);  /* 0–60.00% */
    }
    case ATTR_NPK_NITROGEN:
    case ATTR_NPK_PHOSPHORUS:
    case ATTR_NPK_POTASSIUM: {
        uint16_t v;
        memcpy(&v, value, sizeof(v));
        return (v <= 1999);  /* 0–1999 mg/L */
    }
    case ATTR_CO2_PPM: {
        uint16_t v;
        memcpy(&v, value, sizeof(v));
        return (v >= 400 && v <= 5000);
    }
    case ATTR_PUMP_PCT:
    case ATTR_FAN_PCT:
    case ATTR_LED_R_PCT:
    case ATTR_LED_B_PCT: {
        uint8_t v;
        memcpy(&v, value, sizeof(v));
        return (v <= 100);
    }
    default:
        return true;  /* No specific range check */
    }
}
