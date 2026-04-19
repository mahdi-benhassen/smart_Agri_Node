/*
 * gw_mqtt_client.c — MQTT over TLS with topic-routed pub/sub
 *
 * FIX applied:
 *  7. mqtt_client_publish_health() now calls system_monitor_get_uptime_sec()
 *     and wifi_manager_get_rssi() instead of hardcoding 0 for both values.
 */
#include "gw_mqtt_client.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_check.h"
#include "agri_data_model.h"
#include "system_monitor.h"
#include "wifi_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "MQTT_CLI";

static esp_mqtt_client_handle_t s_client    = NULL;
static mqtt_cmd_callback_t      s_cmd_cb    = NULL;
static bool                     s_connected = false;
static char                     s_farm_id[32] = "farm01";

#define MQTT_BUF_SIZE  4096

/* ── MQTT Event Handler ───────────────────────────────────────────── */
static void mqtt_event_handler(void *args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "Connected to broker");

        char topic[128];
        snprintf(topic, sizeof(topic), "agri/%s/+/cmd", s_farm_id);
        esp_mqtt_client_subscribe(s_client, topic, 1);

        snprintf(topic, sizeof(topic), "agri/%s/+/ota", s_farm_id);
        esp_mqtt_client_subscribe(s_client, topic, 1);

        snprintf(topic, sizeof(topic), "agri/%s/config", s_farm_id);
        esp_mqtt_client_subscribe(s_client, topic, 1);
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Disconnected from broker");
        break;

    case MQTT_EVENT_DATA: {
        ESP_LOGI(TAG, "Data on topic: %.*s", event->topic_len, event->topic);
        if (s_cmd_cb && event->data_len > 0) {
            agri_cmd_t cmd;
            char *json = strndup(event->data, event->data_len);
            if (json) {
                if (agri_cmd_from_json(json, &cmd) == ESP_OK) {
                    s_cmd_cb(&cmd);
                } else {
                    ESP_LOGW(TAG, "Failed to parse command JSON");
                }
                free(json);
            }
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error type=%d", event->error_handle->error_type);
        break;

    default:
        break;
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t mqtt_client_start(const char *broker_uri, const char *username,
                             const char *password, const char *farm_id)
{
    if (!broker_uri) return ESP_ERR_INVALID_ARG;

    if (farm_id) {
        strncpy(s_farm_id, farm_id, sizeof(s_farm_id) - 1);
        s_farm_id[sizeof(s_farm_id) - 1] = '\0';
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri                   = broker_uri,
        .credentials.username                 = username,
        .credentials.authentication.password  = password,
        .buffer.size                          = MQTT_BUF_SIZE,
        .buffer.out_size                      = MQTT_BUF_SIZE,
        .network.reconnect_timeout_ms         = 5000,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) { ESP_LOGE(TAG, "Client init failed"); return ESP_ERR_NO_MEM; }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_err_t ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Client start failed: %s", esp_err_to_name(ret)); return ret; }

    ESP_LOGI(TAG, "MQTT client started — broker: %s  farm: %s", broker_uri, s_farm_id);
    return ESP_OK;
}

esp_err_t mqtt_client_publish_telemetry(const char *node_id,
                                         const agri_sensor_data_t *data)
{
    if (!s_connected || !node_id || !data) return ESP_ERR_INVALID_STATE;

    char topic[128];
    snprintf(topic, sizeof(topic), "agri/%s/%s/telemetry", s_farm_id, node_id);

    char json[MQTT_BUF_SIZE];
    esp_err_t ret = agri_data_to_json(data, json, sizeof(json));
    if (ret != ESP_OK) return ret;

    int msg_id = esp_mqtt_client_publish(s_client, topic, json, 0, 1, 0);
    if (msg_id < 0) { ESP_LOGW(TAG, "Publish failed on %s", topic); return ESP_FAIL; }

    ESP_LOGD(TAG, "Published telemetry: %s (msg_id=%d)", topic, msg_id);
    return ESP_OK;
}

esp_err_t mqtt_client_publish_status(const char *node_id, const char *status)
{
    if (!s_connected || !node_id || !status) return ESP_ERR_INVALID_STATE;

    char topic[128];
    snprintf(topic, sizeof(topic), "agri/%s/%s/status", s_farm_id, node_id);

    int msg_id = esp_mqtt_client_publish(s_client, topic, status, 0, 1, 1);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

/* FIX 7: Use real uptime and RSSI instead of hardcoded zeroes */
esp_err_t mqtt_client_publish_health(void)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;

    char topic[128];
    snprintf(topic, sizeof(topic), "agri/%s/gw/health", s_farm_id);

    char json[256];
    snprintf(json, sizeof(json),
             "{\"heap\":%lu,\"min_heap\":%lu,\"uptime\":%lu,\"rssi\":%d}",
             (unsigned long)system_monitor_get_free_heap(),
             (unsigned long)system_monitor_get_min_heap(),
             (unsigned long)system_monitor_get_uptime_sec(),
             (int)wifi_manager_get_rssi());

    int msg_id = esp_mqtt_client_publish(s_client, topic, json, 0, 0, 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

void mqtt_client_set_cmd_callback(mqtt_cmd_callback_t cb) { s_cmd_cb = cb; }
bool mqtt_client_is_connected(void)                        { return s_connected; }

void mqtt_client_stop(void)
{
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client    = NULL;
        s_connected = false;
        ESP_LOGI(TAG, "MQTT client stopped");
    }
}
