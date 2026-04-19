/*
 * Gateway — main.c
 *
 * FIX applied:
 *  13. mqtt_pub_task cloud-buffer replay now parses the buffered JSON with
 *      agri_data_from_json() and re-publishes via mqtt_client_publish_telemetry()
 *      instead of reading and silently discarding all buffered entries.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_task_wdt.h"

#include "nvs_config.h"
#include "system_monitor.h"
#include "sntp_sync.h"
#include "wifi_manager.h"
#include "zigbee_coordinator.h"
#include "gw_mqtt_client.h"
#include "rest_api.h"
#include "cloud_buffer.h"
#include "ota_manager.h"
#include "agri_data_model.h"

static const char *TAG = "SAGRI_GW";

#define FW_VERSION  "1.4.2"

static QueueHandle_t s_telemetry_queue = NULL;

#define TELEMETRY_QUEUE_LEN  64

typedef struct {
    agri_sensor_data_t data;
    uint16_t           short_addr;
} telemetry_item_t;

/* ── Zigbee → queue ─────────────────────────────────────────────────── */
static void on_zigbee_telemetry(const agri_sensor_data_t *data, uint16_t short_addr)
{
    if (s_telemetry_queue && data) {
        telemetry_item_t item;
        memcpy(&item.data, data, sizeof(agri_sensor_data_t));
        item.short_addr = short_addr;
        if (xQueueSend(s_telemetry_queue, &item, pdMS_TO_TICKS(100)) != pdPASS) {
            ESP_LOGW(TAG, "Telemetry queue full — dropping");
        }
    }
}

/* ── MQTT cmd → Zigbee ──────────────────────────────────────────────── */
static void on_mqtt_command(const agri_cmd_t *cmd)
{
    if (!cmd) return;
    ESP_LOGI(TAG, "MQTT cmd → Zigbee: device=%s type=0x%02X",
             cmd->device_id, (int)cmd->cmd_type);

    uint16_t addr = 0;
    const char *hex = strstr(cmd->device_id, "NODE-");
    if (hex) addr = (uint16_t)strtoul(hex + 5, NULL, 16);

    if (addr > 0) {
        zigbee_coordinator_send_cmd(addr, cmd);
    } else {
        ESP_LOGW(TAG, "Cannot resolve address for: %s", cmd->device_id);
    }
}

/* ── MQTT Publisher Task ───────────────────────────────────────────── */
static void mqtt_pub_task(void *arg)
{
    esp_task_wdt_add(NULL);
    telemetry_item_t item;

    while (1) {
        esp_task_wdt_reset();

        /* FIX 13: replay buffered entries — parse JSON and republish */
        if (mqtt_client_is_connected() && cloud_buffer_has_data()) {
            char *json = malloc(AGRI_MAX_JSON_LEN);
            if (json) {
                while (cloud_buffer_read_next(json, AGRI_MAX_JSON_LEN) == ESP_OK) {
                    agri_sensor_data_t buffered;
                    if (agri_data_from_json(json, &buffered) == ESP_OK) {
                        /* device_id doubles as the node_id for topic construction */
                        esp_err_t ret = mqtt_client_publish_telemetry(
                                            buffered.device_id, &buffered);
                        if (ret != ESP_OK) {
                            /* Re-buffer on publish failure, stop replay loop */
                            cloud_buffer_write(json);
                            ESP_LOGW(TAG, "Replay publish failed — re-buffered");
                            break;
                        }
                        ESP_LOGI(TAG, "Replayed buffered entry for %s",
                                 buffered.device_id);
                    } else {
                        ESP_LOGW(TAG, "Buffered JSON parse failed — discarding entry");
                    }
                    esp_task_wdt_reset();
                }
                free(json);
            }
        }

        /* Process new telemetry from Zigbee */
        if (xQueueReceive(s_telemetry_queue, &item, pdMS_TO_TICKS(1000)) == pdPASS) {
            char node_id[16];
            snprintf(node_id, sizeof(node_id), "NODE-%04X", item.short_addr);

            /* Timestamp is set by the staging flush in zigbee_coordinator */
            if (item.data.timestamp[0] == '\0') {
                sntp_sync_get_iso8601(item.data.timestamp,
                                      sizeof(item.data.timestamp));
            }

            if (mqtt_client_is_connected()) {
                esp_err_t ret = mqtt_client_publish_telemetry(node_id, &item.data);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "MQTT publish failed — buffering offline");
                    char *jbuf = malloc(AGRI_MAX_JSON_LEN);
                    if (jbuf) {
                        if (agri_data_to_json(&item.data, jbuf, AGRI_MAX_JSON_LEN) == ESP_OK) {
                            cloud_buffer_write(jbuf);
                        }
                        free(jbuf);
                    }
                }
            } else {
                char *jbuf = malloc(AGRI_MAX_JSON_LEN);
                if (jbuf) {
                    if (agri_data_to_json(&item.data, jbuf, AGRI_MAX_JSON_LEN) == ESP_OK) {
                        cloud_buffer_write(jbuf);
                        ESP_LOGD(TAG, "Buffered (offline): %s", node_id);
                    }
                    free(jbuf);
                }
            }
        }
    }
}

/* ── app_main ──────────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "   Smart-Agri Gateway v%s", FW_VERSION);
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    gw_nvs_config_init();

    char wifi_ssid[64]  = "";
    char wifi_pass[64]  = "";
    char mqtt_uri[128]  = "";
    char mqtt_user[64]  = "";
    char mqtt_pass[64]  = "";
    char farm_id[32]    = "";
    char ota_url[128]   = "";
    char ntp_server[64] = "";

    gw_nvs_config_get_str(GW_NVS_WIFI_SSID,  wifi_ssid,   sizeof(wifi_ssid),   "SAGRI-AP");
    gw_nvs_config_get_str(GW_NVS_WIFI_PASS,  wifi_pass,   sizeof(wifi_pass),   "");
    gw_nvs_config_get_str(GW_NVS_MQTT_URI,   mqtt_uri,    sizeof(mqtt_uri),    "mqtts://broker.example.com:8883");
    gw_nvs_config_get_str(GW_NVS_MQTT_USER,  mqtt_user,   sizeof(mqtt_user),   "");
    gw_nvs_config_get_str(GW_NVS_MQTT_PASS,  mqtt_pass,   sizeof(mqtt_pass),   "");
    gw_nvs_config_get_str(GW_NVS_FARM_ID,    farm_id,     sizeof(farm_id),     "farm01");
    gw_nvs_config_get_str(GW_NVS_OTA_URL,    ota_url,     sizeof(ota_url),     "");
    gw_nvs_config_get_str(GW_NVS_NTP_SERVER, ntp_server,  sizeof(ntp_server),  "pool.ntp.org");

    system_monitor_init();
    sntp_sync_start(ntp_server);
    wifi_manager_start(wifi_ssid, strlen(wifi_pass) > 0 ? wifi_pass : NULL);

    EventGroupHandle_t wifi_eg = wifi_manager_get_event_group();
    if (wifi_eg) {
        xEventGroupWaitBits(wifi_eg, WIFI_EVENT_CONNECTED,
                            pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));
    }

    zigbee_coordinator_start();
    zigbee_coordinator_set_telemetry_cb(on_zigbee_telemetry);

    cloud_buffer_init();

    s_telemetry_queue = xQueueCreate(TELEMETRY_QUEUE_LEN, sizeof(telemetry_item_t));
    if (!s_telemetry_queue) {
        ESP_LOGE(TAG, "FATAL: telemetry queue create failed");
    }

    mqtt_client_start(mqtt_uri,
                       strlen(mqtt_user) > 0 ? mqtt_user : NULL,
                       strlen(mqtt_pass) > 0 ? mqtt_pass : NULL,
                       farm_id);
    mqtt_client_set_cmd_callback(on_mqtt_command);

    xTaskCreatePinnedToCore(mqtt_pub_task, "mqtt_pub", 8192, NULL, 4, NULL, 0);

    rest_api_start();

    if (strlen(ota_url) > 0) {
        ota_manager_start(ota_url);
    }

    ESP_LOGI(TAG, "All services started. Free heap: %lu bytes",
             (unsigned long)esp_get_free_heap_size());
}
