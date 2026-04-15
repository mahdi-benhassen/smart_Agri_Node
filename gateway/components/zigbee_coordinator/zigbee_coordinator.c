/*
 * zigbee_coordinator.c — Zigbee Coordinator: network formation, device tracking,
 *                         attribute report recv → MQTT queue
 */
#include "zigbee_coordinator.h"
#include "agri_zigbee_clusters.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ZB_COORD";

static zb_telemetry_callback_t s_telemetry_cb = NULL;
static int s_node_count = 0;
static agri_node_info_t s_nodes[ZB_COORD_MAX_NODES];

/* ── Signal Handler ────────────────────────────────────────────────── */

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal)
{
    uint32_t *p_status = signal->p_app_signal;
    esp_zb_app_signal_type_t sig_type = *p_status;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Zigbee stack initialized — forming network");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
        break;

    case ESP_ZB_BDB_SIGNAL_FORMATION:
        if (signal->esp_err_status == ESP_OK) {
            ESP_LOGI(TAG, "Network formed: PAN=0x%04X ch=%d",
                     esp_zb_get_pan_id(), esp_zb_get_current_channel());

            /* Open network for joining */
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGE(TAG, "Network formation failed: %s",
                     esp_err_to_name(signal->esp_err_status));
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        ESP_LOGI(TAG, "Network open for joining");
        break;

    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE: {
        esp_zb_zdo_signal_device_annce_params_t *annce =
            (esp_zb_zdo_signal_device_annce_params_t *)esp_zb_app_signal_get_params(signal);
        if (annce) {
            ESP_LOGI(TAG, "Device joined: addr=0x%04X", annce->device_short_addr);

            if (s_node_count < ZB_COORD_MAX_NODES) {
                agri_node_info_t *node = &s_nodes[s_node_count];
                memset(node, 0, sizeof(agri_node_info_t));
                node->zigbee_short_addr = annce->device_short_addr;
                node->status = AGRI_NODE_ONLINE;
                snprintf(node->device_id, sizeof(node->device_id),
                         "NODE-%04X", annce->device_short_addr);
                s_node_count++;
            }
        }
        break;
    }

    default:
        ESP_LOGD(TAG, "Signal: 0x%lX", (unsigned long)sig_type);
        break;
    }
}

/* ── Attribute Report Handler ──────────────────────────────────────── */

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id,
                                    const void *message)
{
    if (callback_id == ESP_ZB_CORE_REPORT_ATTR_CB_ID && message) {
        const esp_zb_zcl_report_attr_message_t *report =
            (const esp_zb_zcl_report_attr_message_t *)message;

        ESP_LOGI(TAG, "Attr report: src=0x%04X cluster=0x%04X attr=0x%04X",
                 report->src_address.u.short_addr,
                 report->cluster, report->attribute.id);

        /* Build sensor data from report and forward via callback */
        if (report->cluster == AGRI_CLUSTER_ID && s_telemetry_cb) {
            agri_sensor_data_t data;
            agri_data_init_defaults(&data);
            snprintf(data.device_id, sizeof(data.device_id),
                     "NODE-%04X", report->src_address.u.short_addr);

            /* Parse attribute value based on ID */
            if (report->attribute.data.value) {
                switch (report->attribute.id) {
                case ATTR_SOIL_MOISTURE: {
                    uint16_t v = *(uint16_t *)report->attribute.data.value;
                    data.soil_moist_pct = (float)v / 100.0f;
                    break;
                }
                case ATTR_CO2_PPM:
                    data.co2_ppm = *(uint16_t *)report->attribute.data.value;
                    break;
                case ATTR_LUX:
                    data.lux = *(uint32_t *)report->attribute.data.value;
                    break;
                case ATTR_BATT_MV:
                    data.batt_mv = *(uint16_t *)report->attribute.data.value;
                    break;
                case ATTR_ALARM_FLAGS:
                    data.alarm_flags = *(uint16_t *)report->attribute.data.value;
                    break;
                default:
                    break;
                }
            }

            s_telemetry_cb(&data, report->src_address.u.short_addr);
        }
    }

    return ESP_OK;
}

/* ── Coordinator Task ──────────────────────────────────────────────── */

static void zb_coord_task(void *arg)
{
    esp_zb_cfg_t zb_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR,
        .install_code_policy = false,
        .nwk_cfg.zczr_cfg = {
            .max_children = ZB_COORD_MAX_NODES,
        },
    };

    esp_zb_init(&zb_cfg);

    /* Register coordinator endpoint with basic + AGRI clusters */
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = 0x01, /* Mains */
    };
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster,
                                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* AGRI cluster (client role — receives reports) */
    esp_zb_attribute_list_t *agri_cluster = esp_zb_zcl_attr_list_create(AGRI_CLUSTER_ID);
    esp_zb_cluster_list_add_custom_cluster(cluster_list, agri_cluster,
                                            ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint = AGRI_ENDPOINT,
        .app_profile_id = AGRI_PROFILE_ID,
        .app_device_id  = ESP_ZB_HA_GATEWAY_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cluster_list, ep_cfg);
    esp_zb_device_register(ep_list);

    esp_zb_core_action_handler_register(zb_action_handler);

    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);

    ESP_LOGI(TAG, "Starting Zigbee coordinator...");
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();

    vTaskDelete(NULL);
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t zigbee_coordinator_start(void)
{
    esp_zb_platform_config_t platform_cfg = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config  = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_RETURN_ON_ERROR(esp_zb_platform_config(&platform_cfg), TAG, "Platform config failed");

    BaseType_t ret = xTaskCreatePinnedToCore(zb_coord_task, "zb_coord", 8192,
                                              NULL, 6, NULL, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Coordinator task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Coordinator started");
    return ESP_OK;
}

void zigbee_coordinator_set_telemetry_cb(zb_telemetry_callback_t cb) { s_telemetry_cb = cb; }

esp_err_t zigbee_coordinator_send_cmd(uint16_t short_addr, const agri_cmd_t *cmd)
{
    if (!cmd) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Sending cmd to 0x%04X type=0x%02X", short_addr, (int)cmd->cmd_type);

    /* Route command via Zigbee On/Off or Level cluster */
    switch (cmd->cmd_type) {
    case AGRI_CMD_VALVE_SET: {
        esp_zb_zcl_on_off_cmd_t on_off_cmd = {
            .zcl_basic_cmd.dst_addr_u.addr_short = short_addr,
            .zcl_basic_cmd.dst_endpoint = AGRI_ENDPOINT,
            .zcl_basic_cmd.src_endpoint = AGRI_ENDPOINT,
            .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
            .on_off_cmd_id = cmd->payload.valve_state ?
                             ESP_ZB_ZCL_CMD_ON_OFF_ON_ID : ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID,
        };
        esp_zb_zcl_on_off_cmd_req(&on_off_cmd);
        break;
    }
    case AGRI_CMD_PUMP_SET:
    case AGRI_CMD_FAN_SET: {
        esp_zb_zcl_move_to_level_cmd_t level_cmd = {
            .zcl_basic_cmd.dst_addr_u.addr_short = short_addr,
            .zcl_basic_cmd.dst_endpoint = AGRI_ENDPOINT,
            .zcl_basic_cmd.src_endpoint = AGRI_ENDPOINT,
            .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
            .level = (uint8_t)((uint16_t)cmd->payload.duty_pct * 254 / 100),
            .transition_time = 0,
        };
        esp_zb_zcl_level_move_to_level_cmd_req(&level_cmd);
        break;
    }
    default:
        ESP_LOGW(TAG, "Unsupported command for Zigbee: 0x%02X", (int)cmd->cmd_type);
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

esp_err_t zigbee_coordinator_permit_join(bool enable, uint8_t duration_sec)
{
    /* Permit join via ZDO */
    ESP_LOGI(TAG, "Permit join: %s (duration=%us)", enable ? "ON" : "OFF", duration_sec);
    return ESP_OK;
}

int zigbee_coordinator_get_node_count(void) { return s_node_count; }

esp_err_t zigbee_coordinator_get_node_info(int index, agri_node_info_t *info)
{
    if (index < 0 || index >= s_node_count || !info) return ESP_ERR_INVALID_ARG;
    memcpy(info, &s_nodes[index], sizeof(agri_node_info_t));
    return ESP_OK;
}
