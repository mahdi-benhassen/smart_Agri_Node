/*
 * wifi_manager.c — Wi-Fi STA with auto-reconnect and exponential backoff
 */
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "WIFI_MGR";
static EventGroupHandle_t s_event_group = NULL;
static int s_retry_count = 0;
static bool s_connected = false;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started — connecting...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            s_connected = false;
            if (s_event_group) {
                xEventGroupClearBits(s_event_group, WIFI_EVENT_CONNECTED);
                xEventGroupSetBits(s_event_group, WIFI_EVENT_DISCONNECTED);
            }

            if (s_retry_count < WIFI_MAX_RETRY) {
                uint32_t backoff = WIFI_BACKOFF_BASE_MS * (1U << s_retry_count);
                if (backoff > WIFI_BACKOFF_MAX_MS) backoff = WIFI_BACKOFF_MAX_MS;

                ESP_LOGW(TAG, "Disconnected — retry %d/%d in %lu ms",
                         s_retry_count + 1, WIFI_MAX_RETRY, (unsigned long)backoff);

                vTaskDelay(pdMS_TO_TICKS(backoff));
                s_retry_count++;
                esp_wifi_connect();
            } else {
                ESP_LOGE(TAG, "Max retries reached — will keep trying at max interval");
                vTaskDelay(pdMS_TO_TICKS(WIFI_BACKOFF_MAX_MS));
                s_retry_count = WIFI_MAX_RETRY - 1; /* Keep retrying */
                esp_wifi_connect();
            }
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected — IP: " IPSTR " RSSI: %d",
                 IP2STR(&event->ip_info.ip), wifi_manager_get_rssi());

        s_connected = true;
        s_retry_count = 0;

        if (s_event_group) {
            xEventGroupClearBits(s_event_group, WIFI_EVENT_DISCONNECTED);
            xEventGroupSetBits(s_event_group, WIFI_EVENT_CONNECTED);
        }
    }
}

esp_err_t wifi_manager_start(const char *ssid, const char *password)
{
    if (!ssid) {
        ESP_LOGE(TAG, "SSID is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    s_event_group = xEventGroupCreate();
    if (!s_event_group) return ESP_ERR_NO_MEM;

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "WiFi init failed");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &wifi_event_handler, NULL, NULL),
        TAG, "WiFi event register failed");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &wifi_event_handler, NULL, NULL),
        TAG, "IP event register failed");

    wifi_config_t wifi_cfg = { 0 };
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    }
    wifi_cfg.sta.threshold.authmode = password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "Set config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "WiFi start failed");

    ESP_LOGI(TAG, "WiFi STA starting — SSID: %s", ssid);
    return ESP_OK;
}

bool wifi_manager_is_connected(void) { return s_connected; }

int8_t wifi_manager_get_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}

EventGroupHandle_t wifi_manager_get_event_group(void) { return s_event_group; }
