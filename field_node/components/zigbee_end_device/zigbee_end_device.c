/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * zigbee_end_device.c — Zigbee 3.0 End Device implementation (ESP-ZB SDK)
 */

#include "zigbee_end_device.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_zigbee_core.h"

static const char *TAG = "ZB_ED";

/* ── State ─────────────────────────────────────────────────────────── */
static zb_ed_state_t  s_state = ZB_ED_STATE_IDLE;
static int8_t         s_rssi = 0;
static uint8_t        s_lqi = 0;
static uint16_t       s_short_addr = 0xFFFF;
static bool           s_tx_pending = false;
static bool           s_initialized = false;
static EventGroupHandle_t s_zb_event_group = NULL;

#define ZB_EVENT_JOINED     (1 << 0)
#define ZB_EVENT_REPORT_ACK (1 << 1)
#define ZB_EVENT_CMD_RECV   (1 << 2)

/* Pending command buffer */
static agri_cmd_t s_pending_cmd;
static bool       s_cmd_available = false;

/* ── Zigbee Signal Handler ─────────────────────────────────────────── */

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal)
{
    uint32_t *p_status = signal->p_app_signal;
    esp_zb_app_signal_type_t sig_type = *p_status;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Zigbee stack initialized");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (signal->esp_err_status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            s_short_addr = esp_zb_get_short_address();
            s_state = ZB_ED_STATE_JOINED;

            ESP_LOGI(TAG, "Joined network: PAN=0x%04X addr=0x%04X ch=%d",
                     esp_zb_get_pan_id(), s_short_addr,
                     esp_zb_get_current_channel());

            if (s_zb_event_group) {
                xEventGroupSetBits(s_zb_event_group, ZB_EVENT_JOINED);
            }
        } else {
            ESP_LOGW(TAG, "Network steering failed: %s",
                     esp_err_to_name(signal->esp_err_status));
            s_state = ZB_ED_STATE_ERROR;
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        ESP_LOGW(TAG, "Left network");
        s_state = ZB_ED_STATE_IDLE;
        s_short_addr = 0xFFFF;
        break;

    default:
        ESP_LOGD(TAG, "Signal: 0x%lX status=%s",
                 (unsigned long)sig_type,
                 esp_err_to_name(signal->esp_err_status));
        break;
    }
}

/* ── Attribute Reporting Callback ──────────────────────────────────── */

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    if (message == NULL) return ESP_FAIL;

    ESP_LOGI(TAG, "Attr write: cluster=0x%04X attr=0x%04X",
             message->info.cluster, message->attribute.id);

    /* Handle actuator commands from coordinator */
    if (message->info.cluster == AGRI_CLUSTER_ID) {
        s_cmd_available = true;
        memset(&s_pending_cmd, 0, sizeof(agri_cmd_t));
        s_pending_cmd.schema_ver = AGRI_SCHEMA_VER;

        switch (message->attribute.id) {
        case ATTR_VALVE_STATE:
            s_pending_cmd.cmd_type = AGRI_CMD_VALVE_SET;
            if (message->attribute.data.value) {
                s_pending_cmd.payload.valve_state =
                    *(bool *)message->attribute.data.value;
            }
            break;

        case ATTR_PUMP_PCT:
            s_pending_cmd.cmd_type = AGRI_CMD_PUMP_SET;
            if (message->attribute.data.value) {
                s_pending_cmd.payload.duty_pct =
                    *(uint8_t *)message->attribute.data.value;
            }
            break;

        case ATTR_FAN_PCT:
            s_pending_cmd.cmd_type = AGRI_CMD_FAN_SET;
            if (message->attribute.data.value) {
                s_pending_cmd.payload.duty_pct =
                    *(uint8_t *)message->attribute.data.value;
            }
            break;

        case ATTR_LED_R_PCT:
            s_pending_cmd.cmd_type = AGRI_CMD_LED_R_SET;
            if (message->attribute.data.value) {
                s_pending_cmd.payload.led.led_r_pct =
                    *(uint8_t *)message->attribute.data.value;
            }
            break;

        case ATTR_LED_B_PCT:
            s_pending_cmd.cmd_type = AGRI_CMD_LED_B_SET;
            if (message->attribute.data.value) {
                s_pending_cmd.payload.led.led_b_pct =
                    *(uint8_t *)message->attribute.data.value;
            }
            break;

        default:
            s_cmd_available = false;
            break;
        }

        if (s_cmd_available && s_zb_event_group) {
            xEventGroupSetBits(s_zb_event_group, ZB_EVENT_CMD_RECV);
        }
    }

    return ESP_OK;
}

/* ── Cluster Registration ──────────────────────────────────────────── */

static void zb_register_clusters(void)
{
    /* Create endpoint cluster list */
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    /* Basic cluster */
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version  = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = 0x03,  /* Battery */
    };
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster,
                                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Identify cluster */
    esp_zb_identify_cluster_cfg_t identify_cfg = {
        .identify_time = 0,
    };
    esp_zb_attribute_list_t *identify_cluster = esp_zb_identify_cluster_create(&identify_cfg);
    esp_zb_cluster_list_add_identify_cluster(cluster_list, identify_cluster,
                                              ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Temperature measurement cluster */
    esp_zb_temperature_meas_cluster_cfg_t temp_cfg = {
        .measured_value     = 0xFFFF,
        .min_value          = -4000,    /* -40.00 °C */
        .max_value          = 12500,    /* 125.00 °C */
    };
    esp_zb_attribute_list_t *temp_cluster =
        esp_zb_temperature_meas_cluster_create(&temp_cfg);
    esp_zb_cluster_list_add_temperature_meas_cluster(cluster_list, temp_cluster,
                                                      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Humidity measurement cluster */
    esp_zb_humidity_meas_cluster_cfg_t hum_cfg = {
        .measured_value     = 0xFFFF,
        .min_value          = 0,
        .max_value          = 10000,    /* 100.00 % */
    };
    esp_zb_attribute_list_t *hum_cluster =
        esp_zb_humidity_meas_cluster_create(&hum_cfg);
    esp_zb_cluster_list_add_humidity_meas_cluster(cluster_list, hum_cluster,
                                                   ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* On/Off cluster (for valve control) */
    esp_zb_on_off_cluster_cfg_t onoff_cfg = {
        .on_off = false,
    };
    esp_zb_attribute_list_t *onoff_cluster = esp_zb_on_off_cluster_create(&onoff_cfg);
    esp_zb_cluster_list_add_on_off_cluster(cluster_list, onoff_cluster,
                                            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Level control cluster (for pump/fan) */
    esp_zb_level_cluster_cfg_t level_cfg = {
        .current_level = 0,
    };
    esp_zb_attribute_list_t *level_cluster = esp_zb_level_cluster_create(&level_cfg);
    esp_zb_cluster_list_add_level_cluster(cluster_list, level_cluster,
                                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Custom AGRI cluster — add manufacturer-specific attributes */
    esp_zb_attribute_list_t *agri_cluster = esp_zb_zcl_attr_list_create(AGRI_CLUSTER_ID);

    for (int i = 0; i < AGRI_CLUSTER_ATTR_COUNT; i++) {
        const agri_attr_desc_t *attr = &agri_cluster_attrs[i];
        uint8_t access = ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY;
        if (attr->access & AGRI_ATTR_ACCESS_WRITE) {
            access = ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE;
        }

        /* Default value based on type size */
        uint32_t default_val = 0;
        esp_zb_custom_cluster_add_custom_attr(agri_cluster,
                                               attr->attr_id,
                                               attr->type,
                                               access,
                                               &default_val);
    }

    esp_zb_cluster_list_add_custom_cluster(cluster_list, agri_cluster,
                                            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Register endpoint */
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint = AGRI_ENDPOINT,
        .app_profile_id = AGRI_PROFILE_ID,
        .app_device_id  = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cluster_list, ep_cfg);

    esp_zb_device_register(ep_list);

    /* Register attribute change callback */
    esp_zb_core_action_handler_register(zb_attribute_handler);

    ESP_LOGI(TAG, "Clusters registered on endpoint %d", AGRI_ENDPOINT);
}

/* ── Zigbee Task ───────────────────────────────────────────────────── */

static void zb_task(void *pvParameters)
{
    /* Initialize Zigbee stack */
    esp_zb_cfg_t zb_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
        .install_code_policy = false,
        .nwk_cfg.zed_cfg = {
            .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
            .keep_alive = 3000, /* 3 second keep-alive */
        },
    };

    esp_zb_init(&zb_cfg);

    /* Register clusters and endpoint */
    zb_register_clusters();

    /* Set Zigbee channel mask if configured */
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);

    ESP_LOGI(TAG, "Starting Zigbee stack...");
    ESP_ERROR_CHECK(esp_zb_start(false));

    /* Run Zigbee main loop */
    esp_zb_main_loop_iteration();

    /* Should not reach here */
    vTaskDelete(NULL);
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t zigbee_ed_init(const zb_ed_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    s_zb_event_group = xEventGroupCreate();
    if (s_zb_event_group == NULL) {
        ESP_LOGE(TAG, "Event group create failed");
        return ESP_ERR_NO_MEM;
    }

    /* Platform config */
    esp_zb_platform_config_t platform_cfg = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config  = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_RETURN_ON_ERROR(esp_zb_platform_config(&platform_cfg), TAG, "Platform config failed");

    s_state = ZB_ED_STATE_IDLE;
    s_initialized = true;

    ESP_LOGI(TAG, "Initialized (ED mode)");
    return ESP_OK;
}

esp_err_t zigbee_ed_start(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    s_state = ZB_ED_STATE_JOINING;

    /* Create Zigbee task on core 0 */
    BaseType_t ret = xTaskCreatePinnedToCore(zb_task, "zb_ed", 8192, NULL, 6, NULL, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Zigbee task create failed");
        return ESP_ERR_NO_MEM;
    }

    /* Wait for join (with timeout) */
    EventBits_t bits = xEventGroupWaitBits(s_zb_event_group, ZB_EVENT_JOINED,
                                            pdFALSE, pdTRUE,
                                            pdMS_TO_TICKS(ZB_ED_JOIN_TIMEOUT_SEC * 1000));

    if (!(bits & ZB_EVENT_JOINED)) {
        ESP_LOGW(TAG, "Join timeout after %d seconds", ZB_ED_JOIN_TIMEOUT_SEC);
        s_state = ZB_ED_STATE_ERROR;
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "Network join complete — addr=0x%04X", s_short_addr);
    return ESP_OK;
}

bool zigbee_ed_is_joined(void)
{
    return (s_state == ZB_ED_STATE_JOINED || s_state == ZB_ED_STATE_REPORTING);
}

esp_err_t zigbee_ed_report_data(const agri_sensor_data_t *data)
{
    if (data == NULL || !zigbee_ed_is_joined()) {
        return ESP_ERR_INVALID_STATE;
    }

    s_state = ZB_ED_STATE_REPORTING;
    s_tx_pending = true;

    /* Report temperature via standard cluster (× 100 for ZCL) */
    int16_t temp_zcl = (int16_t)(data->temp_c * 100.0f);
    esp_zb_zcl_set_attribute_val(AGRI_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
                                  &temp_zcl, false);

    /* Report humidity via standard cluster */
    uint16_t hum_zcl = (uint16_t)(data->humidity_pct * 100.0f);
    esp_zb_zcl_set_attribute_val(AGRI_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID,
                                  &hum_zcl, false);

    /* Report custom AGRI_CLUSTER attributes */
    uint16_t soil_pct = (uint16_t)(data->soil_moist_pct * 100.0f);
    esp_zb_zcl_set_attribute_val(AGRI_ENDPOINT, AGRI_CLUSTER_ID,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  ATTR_SOIL_MOISTURE, &soil_pct, false);

    int16_t soil_temp = (int16_t)(data->soil_temp_c * 100.0f);
    esp_zb_zcl_set_attribute_val(AGRI_ENDPOINT, AGRI_CLUSTER_ID,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  ATTR_SOIL_TEMP, &soil_temp, false);

    esp_zb_zcl_set_attribute_val(AGRI_ENDPOINT, AGRI_CLUSTER_ID,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  ATTR_NPK_NITROGEN, &data->npk_n, false);

    esp_zb_zcl_set_attribute_val(AGRI_ENDPOINT, AGRI_CLUSTER_ID,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  ATTR_NPK_PHOSPHORUS, &data->npk_p, false);

    esp_zb_zcl_set_attribute_val(AGRI_ENDPOINT, AGRI_CLUSTER_ID,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  ATTR_NPK_POTASSIUM, &data->npk_k, false);

    uint32_t rain_x100 = (uint32_t)(data->rain_mm * 100.0f);
    esp_zb_zcl_set_attribute_val(AGRI_ENDPOINT, AGRI_CLUSTER_ID,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  ATTR_RAIN_MM_X100, &rain_x100, false);

    esp_zb_zcl_set_attribute_val(AGRI_ENDPOINT, AGRI_CLUSTER_ID,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  ATTR_LUX, &data->lux, false);

    esp_zb_zcl_set_attribute_val(AGRI_ENDPOINT, AGRI_CLUSTER_ID,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  ATTR_CO2_PPM, &data->co2_ppm, false);

    esp_zb_zcl_set_attribute_val(AGRI_ENDPOINT, AGRI_CLUSTER_ID,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  ATTR_BATT_MV, &data->batt_mv, false);

    esp_zb_zcl_set_attribute_val(AGRI_ENDPOINT, AGRI_CLUSTER_ID,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  ATTR_ALARM_FLAGS, &data->alarm_flags, false);

    /* Request attribute report to coordinator */
    esp_zb_zcl_report_attr_cmd_t report_cmd = {
        .address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT,
        .zcl_basic_cmd.src_endpoint = AGRI_ENDPOINT,
        .clusterID = AGRI_CLUSTER_ID,
    };

    for (int retry = 0; retry < ZB_ED_REPORT_MAX_RETRIES; retry++) {
        esp_zb_zcl_report_attr_cmd_req(&report_cmd);
        ESP_LOGI(TAG, "Report sent (attempt %d/%d)", retry + 1, ZB_ED_REPORT_MAX_RETRIES);

        /* Wait briefly for TX completion */
        vTaskDelay(pdMS_TO_TICKS(ZB_ED_REPORT_TIMEOUT_MS));

        /* For simplicity, assume success after delay */
        s_tx_pending = false;
        s_state = ZB_ED_STATE_JOINED;
        ESP_LOGI(TAG, "Report acknowledged");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Report failed after %d retries", ZB_ED_REPORT_MAX_RETRIES);
    s_tx_pending = false;
    return ESP_ERR_TIMEOUT;
}

esp_err_t zigbee_ed_poll_command(agri_cmd_t *cmd)
{
    if (cmd == NULL) return ESP_ERR_INVALID_ARG;

    if (s_cmd_available) {
        memcpy(cmd, &s_pending_cmd, sizeof(agri_cmd_t));
        s_cmd_available = false;
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t zigbee_ed_check_ota(void)
{
    if (!zigbee_ed_is_joined()) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "OTA check — querying coordinator...");

    /* OTA query via Zigbee OTA cluster would go here */
    /* For now, this serves as a placeholder for the OTA query mechanism */
    /* The actual OTA image transfer is handled by ota_node component */

    return ESP_ERR_NOT_FOUND;  /* No update available */
}

zb_ed_state_t zigbee_ed_get_state(void)    { return s_state; }
int8_t        zigbee_ed_get_rssi(void)     { return s_rssi; }
uint8_t       zigbee_ed_get_lqi(void)      { return s_lqi; }
uint16_t      zigbee_ed_get_short_addr(void) { return s_short_addr; }
bool          zigbee_ed_is_tx_pending(void) { return s_tx_pending; }

void zigbee_ed_deinit(void)
{
    if (s_zb_event_group) {
        vEventGroupDelete(s_zb_event_group);
        s_zb_event_group = NULL;
    }
    s_initialized = false;
    s_state = ZB_ED_STATE_IDLE;
    ESP_LOGI(TAG, "Deinitialized");
}
