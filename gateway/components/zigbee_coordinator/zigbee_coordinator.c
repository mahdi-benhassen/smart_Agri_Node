/*
 * zigbee_coordinator.c — Zigbee Coordinator: network formation, device tracking,
 *                         attribute report recv → MQTT queue
 *
 * FIXES applied:
 *  1. All 15 AGRI_CLUSTER attributes are now parsed (previously only 5).
 *  2. Per-node staging buffer accumulates all attributes from one reporting
 *     cycle before forwarding via the telemetry callback.
 *  3. s_nodes[] and s_node_count protected by a mutex (REST API reads vs
 *     Zigbee task writes).
 *  4. Device-announce handler deduplicates rejoining nodes instead of
 *     blindly appending.
 */
#include "zigbee_coordinator.h"
#include "agri_zigbee_clusters.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sntp_sync.h"
#include <string.h>

static const char *TAG = "ZB_COORD";

/* ── Node registry ─────────────────────────────────────────────────── */
static SemaphoreHandle_t    s_nodes_mutex  = NULL;
static int                  s_node_count   = 0;
static agri_node_info_t     s_nodes[ZB_COORD_MAX_NODES];

/* ── Per-node staging buffers ──────────────────────────────────────── */
/* One staging entry per connected node — accumulates attributes within
   a single reporting cycle before the full struct is forwarded.          */
typedef struct {
    uint16_t           short_addr;
    bool               in_use;
    agri_sensor_data_t data;
    int64_t            last_attr_time_ms;
} node_staging_t;

#define MAX_STAGING_NODES  ZB_COORD_MAX_NODES
#define STAGING_FLUSH_MS   2000   /* Forward after 2 s of no new attrs */

static node_staging_t s_staging[MAX_STAGING_NODES];

/* ── Callback ──────────────────────────────────────────────────────── */
static zb_telemetry_callback_t s_telemetry_cb = NULL;

/* ── Internal: find or create a staging slot ────────────────────────── */
static node_staging_t *staging_get(uint16_t short_addr)
{
    /* Find existing */
    for (int i = 0; i < MAX_STAGING_NODES; i++) {
        if (s_staging[i].in_use && s_staging[i].short_addr == short_addr) {
            return &s_staging[i];
        }
    }
    /* Allocate new */
    for (int i = 0; i < MAX_STAGING_NODES; i++) {
        if (!s_staging[i].in_use) {
            memset(&s_staging[i], 0, sizeof(node_staging_t));
            s_staging[i].in_use     = true;
            s_staging[i].short_addr = short_addr;
            agri_data_init_defaults(&s_staging[i].data);
            snprintf(s_staging[i].data.device_id,
                     sizeof(s_staging[i].data.device_id),
                     "NODE-%04X", short_addr);
            return &s_staging[i];
        }
    }
    return NULL;  /* All slots full */
}

/* ── Internal: find a node in the registry ──────────────────────────── */
static int find_node_by_addr(uint16_t short_addr)
{
    for (int i = 0; i < s_node_count; i++) {
        if (s_nodes[i].zigbee_short_addr == short_addr) return i;
    }
    return -1;
}

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
            (esp_zb_zdo_signal_device_annce_params_t *)
            esp_zb_app_signal_get_params(signal);
        if (!annce) break;

        uint16_t addr = annce->device_short_addr;
        ESP_LOGI(TAG, "Device announce: addr=0x%04X", addr);

        xSemaphoreTake(s_nodes_mutex, portMAX_DELAY);

        int existing = find_node_by_addr(addr);
        if (existing >= 0) {
            /* Rejoin — update status, don't add a duplicate */
            s_nodes[existing].status = AGRI_NODE_ONLINE;
            sntp_sync_get_iso8601(s_nodes[existing].last_seen,
                                  sizeof(s_nodes[existing].last_seen));
            ESP_LOGI(TAG, "Node 0x%04X rejoined (slot %d)", addr, existing);
        } else if (s_node_count < ZB_COORD_MAX_NODES) {
            agri_node_info_t *node = &s_nodes[s_node_count];
            memset(node, 0, sizeof(agri_node_info_t));
            node->zigbee_short_addr = addr;
            node->status = AGRI_NODE_ONLINE;
            snprintf(node->device_id, sizeof(node->device_id), "NODE-%04X", addr);
            sntp_sync_get_iso8601(node->last_seen, sizeof(node->last_seen));
            s_node_count++;
            ESP_LOGI(TAG, "New node registered: 0x%04X (total %d)", addr, s_node_count);
        } else {
            ESP_LOGW(TAG, "Node table full (%d) — cannot register 0x%04X",
                     ZB_COORD_MAX_NODES, addr);
        }

        xSemaphoreGive(s_nodes_mutex);
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
    if (callback_id != ESP_ZB_CORE_REPORT_ATTR_CB_ID || !message) return ESP_OK;

    const esp_zb_zcl_report_attr_message_t *report =
        (const esp_zb_zcl_report_attr_message_t *)message;

    uint16_t addr    = report->src_address.u.short_addr;
    uint16_t cluster = report->cluster;
    uint16_t attr_id = report->attribute.id;
    const void *val  = report->attribute.data.value;

    ESP_LOGD(TAG, "Attr report: src=0x%04X cluster=0x%04X attr=0x%04X",
             addr, cluster, attr_id);

    /* ── Standard temperature cluster ─────────────────────────────── */
    if (cluster == ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT &&
        attr_id == ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID && val) {
        node_staging_t *s = staging_get(addr);
        if (s) {
            s->data.temp_c = (float)(*(int16_t *)val) / 100.0f;
            s->last_attr_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }
        return ESP_OK;
    }

    /* ── Standard humidity cluster ────────────────────────────────── */
    if (cluster == ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT &&
        attr_id == ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID && val) {
        node_staging_t *s = staging_get(addr);
        if (s) {
            s->data.humidity_pct = (float)(*(uint16_t *)val) / 100.0f;
            s->last_attr_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }
        return ESP_OK;
    }

    /* ── Custom AGRI cluster ───────────────────────────────────────── */
    if (cluster != AGRI_CLUSTER_ID || !val) return ESP_OK;

    node_staging_t *s = staging_get(addr);
    if (!s) {
        ESP_LOGW(TAG, "No staging slot for 0x%04X", addr);
        return ESP_OK;
    }

    s->last_attr_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    switch (attr_id) {
    case ATTR_SOIL_MOISTURE: {
        uint16_t v = *(uint16_t *)val;
        s->data.soil_moist_pct = (float)v / 100.0f;
        break;
    }
    case ATTR_SOIL_TEMP: {
        int16_t v = *(int16_t *)val;
        s->data.soil_temp_c = (float)v / 100.0f;
        break;
    }
    case ATTR_NPK_NITROGEN:
        s->data.npk_n = *(uint16_t *)val;
        break;
    case ATTR_NPK_PHOSPHORUS:
        s->data.npk_p = *(uint16_t *)val;
        break;
    case ATTR_NPK_POTASSIUM:
        s->data.npk_k = *(uint16_t *)val;
        break;
    case ATTR_RAIN_MM_X100: {
        uint32_t v = *(uint32_t *)val;
        s->data.rain_mm = (float)v / 100.0f;
        break;
    }
    case ATTR_LUX:
        s->data.lux = *(uint32_t *)val;
        break;
    case ATTR_CO2_PPM:
        s->data.co2_ppm = *(uint16_t *)val;
        break;
    case ATTR_BATT_MV:
        s->data.batt_mv = *(uint16_t *)val;
        break;
    case ATTR_VALVE_STATE:
        s->data.valve_open = *(bool *)val;
        break;
    case ATTR_PUMP_PCT:
        s->data.pump_pct = *(uint8_t *)val;
        break;
    case ATTR_FAN_PCT:
        s->data.fan_pct = *(uint8_t *)val;
        break;
    case ATTR_LED_R_PCT:
        s->data.led_r_pct = *(uint8_t *)val;
        break;
    case ATTR_LED_B_PCT:
        s->data.led_b_pct = *(uint8_t *)val;
        break;
    case ATTR_ALARM_FLAGS:
        s->data.alarm_flags = *(uint16_t *)val;
        break;
    default:
        ESP_LOGD(TAG, "Unknown AGRI attr 0x%04X", attr_id);
        break;
    }

    /* Update node registry with latest link quality and timestamp */
    xSemaphoreTake(s_nodes_mutex, portMAX_DELAY);
    int idx = find_node_by_addr(addr);
    if (idx >= 0) {
        s_nodes[idx].batt_mv     = s->data.batt_mv;
        s_nodes[idx].alarm_flags = s->data.alarm_flags;
        sntp_sync_get_iso8601(s_nodes[idx].last_seen, sizeof(s_nodes[idx].last_seen));
    }
    xSemaphoreGive(s_nodes_mutex);

    return ESP_OK;
}

/* ── Staging flush task — forwards complete sensor packets ─────────── */
static void staging_flush_task(void *arg)
{
    while (1) {
        int64_t now_ms = (int64_t)(xTaskGetTickCount()) * portTICK_PERIOD_MS;

        for (int i = 0; i < MAX_STAGING_NODES; i++) {
            if (!s_staging[i].in_use) continue;
            if (s_staging[i].last_attr_time_ms == 0) continue;

            int64_t age_ms = now_ms - s_staging[i].last_attr_time_ms;
            if (age_ms >= STAGING_FLUSH_MS) {
                /* Enough time has elapsed — forward the staged data */
                if (s_telemetry_cb) {
                    sntp_sync_get_iso8601(s_staging[i].data.timestamp,
                                          sizeof(s_staging[i].data.timestamp));
                    s_telemetry_cb(&s_staging[i].data, s_staging[i].short_addr);
                }
                /* Reset for next cycle — keep the slot allocated */
                agri_data_init_defaults(&s_staging[i].data);
                snprintf(s_staging[i].data.device_id,
                         sizeof(s_staging[i].data.device_id),
                         "NODE-%04X", s_staging[i].short_addr);
                s_staging[i].last_attr_time_ms = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ── Coordinator Task ──────────────────────────────────────────────── */
static void zb_coord_task(void *arg)
{
    esp_zb_cfg_t zb_cfg = {
        .esp_zb_role         = ESP_ZB_DEVICE_TYPE_COORDINATOR,
        .install_code_policy = false,
        .nwk_cfg.zczr_cfg    = { .max_children = ZB_COORD_MAX_NODES },
    };

    esp_zb_init(&zb_cfg);

    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version  = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = 0x01,
    };
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster,
                                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_attribute_list_t *agri_cluster = esp_zb_zcl_attr_list_create(AGRI_CLUSTER_ID);
    esp_zb_cluster_list_add_custom_cluster(cluster_list, agri_cluster,
                                            ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint           = AGRI_ENDPOINT,
        .app_profile_id     = AGRI_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_GATEWAY_DEVICE_ID,
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
    s_nodes_mutex = xSemaphoreCreateMutex();
    if (!s_nodes_mutex) {
        ESP_LOGE(TAG, "Mutex create failed");
        return ESP_ERR_NO_MEM;
    }

    memset(s_staging, 0, sizeof(s_staging));

    esp_zb_platform_config_t platform_cfg = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config  = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_RETURN_ON_ERROR(esp_zb_platform_config(&platform_cfg), TAG, "Platform config failed");

    /* Staging flush task */
    BaseType_t r = xTaskCreatePinnedToCore(staging_flush_task, "zb_flush", 4096,
                                            NULL, 4, NULL, 0);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "Staging flush task create failed");
        return ESP_ERR_NO_MEM;
    }

    /* Zigbee main task */
    r = xTaskCreatePinnedToCore(zb_coord_task, "zb_coord", 8192, NULL, 6, NULL, 1);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "Coordinator task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Coordinator started");
    return ESP_OK;
}

void zigbee_coordinator_set_telemetry_cb(zb_telemetry_callback_t cb)
{
    s_telemetry_cb = cb;
}

esp_err_t zigbee_coordinator_send_cmd(uint16_t short_addr, const agri_cmd_t *cmd)
{
    if (!cmd) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Sending cmd to 0x%04X type=0x%02X", short_addr, (int)cmd->cmd_type);

    switch (cmd->cmd_type) {
    case AGRI_CMD_VALVE_SET: {
        esp_zb_zcl_on_off_cmd_t c = {
            .zcl_basic_cmd.dst_addr_u.addr_short = short_addr,
            .zcl_basic_cmd.dst_endpoint          = AGRI_ENDPOINT,
            .zcl_basic_cmd.src_endpoint          = AGRI_ENDPOINT,
            .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
            .on_off_cmd_id = cmd->payload.valve_state ?
                             ESP_ZB_ZCL_CMD_ON_OFF_ON_ID :
                             ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID,
        };
        esp_zb_zcl_on_off_cmd_req(&c);
        break;
    }
    case AGRI_CMD_PUMP_SET:
    case AGRI_CMD_FAN_SET: {
        esp_zb_zcl_move_to_level_cmd_t c = {
            .zcl_basic_cmd.dst_addr_u.addr_short = short_addr,
            .zcl_basic_cmd.dst_endpoint          = AGRI_ENDPOINT,
            .zcl_basic_cmd.src_endpoint          = AGRI_ENDPOINT,
            .address_mode    = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
            .level           = (uint8_t)((uint16_t)cmd->payload.duty_pct * 254 / 100),
            .transition_time = 0,
        };
        esp_zb_zcl_level_move_to_level_cmd_req(&c);
        break;
    }
    default:
        ESP_LOGW(TAG, "Unsupported Zigbee command: 0x%02X", (int)cmd->cmd_type);
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

esp_err_t zigbee_coordinator_permit_join(bool enable, uint8_t duration_sec)
{
    ESP_LOGI(TAG, "Permit join: %s (duration=%us)",
             enable ? "ON" : "OFF", duration_sec);
    return ESP_OK;
}

int zigbee_coordinator_get_node_count(void)
{
    xSemaphoreTake(s_nodes_mutex, portMAX_DELAY);
    int c = s_node_count;
    xSemaphoreGive(s_nodes_mutex);
    return c;
}

esp_err_t zigbee_coordinator_get_node_info(int index, agri_node_info_t *info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_nodes_mutex, portMAX_DELAY);
    if (index < 0 || index >= s_node_count) {
        xSemaphoreGive(s_nodes_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(info, &s_nodes[index], sizeof(agri_node_info_t));
    xSemaphoreGive(s_nodes_mutex);
    return ESP_OK;
}
