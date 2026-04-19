/*
 * wifi_manager.c — Wi-Fi STA with auto-reconnect and exponential backoff
 *
 * FIX applied:
 *  6. vTaskDelay() was called inside the esp_event_loop task handler, which
 *     blocked ALL event dispatching (MQTT, Zigbee, IP events) for up to 60s.
 *     Replaced with a FreeRTOS software timer that fires after the backoff
 *     period and then calls esp_wifi_connect() from the timer task, leaving
 *     the event loop completely unblocked.
 */
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include <string.h>

static const char *TAG = "WIFI_MGR";

static EventGroupHandle_t s_event_group   = NULL;
static TimerHandle_t      s_reconnect_tmr = NULL;
static int                s_retry_count   = 0;
static bool               s_connected     = false;

/* ── Timer callback: runs in the FreeRTOS timer task ───────────────── */
static void reconnect_timer_cb(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Reconnect attempt %d/%d", s_retry_count, WIFI_MAX_RETRY);
    esp_wifi_connect();
}

/* ── Compute backoff and arm the timer ─────────────────────────────── */
static void schedule_reconnect(void)
{
    uint32_t backoff_ms = WIFI_BACKOFF_BASE_MS * (1U << s_retry_count);
    if (backoff_ms > WIFI_BACKOFF_MAX_MS) backoff_ms = WIFI_BACKOFF_MAX_MS;

    ESP_LOGW(TAG, "Disconnected — retrying in %lu ms (attempt %d)",
             (unsigned long)backoff_ms, s_retry_count + 1);

    s_retry_count++;
    if (s_retry_count > WIFI_MAX_RETRY) {
        /* Keep at max interval indefinitely */
        s_retry_count = WIFI_MAX_RETRY;
    }

    /* One-shot timer; period is changed each call */
    xTimerChangePeriod(s_reconnect_tmr, pdMS_TO_TICKS(backoff_ms), 0);
    xTimerStart(s_reconnect_tmr, 0);
}

/* ── Event Handler ─────────────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started — connecting...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            s_connected = false;
            if (s_event_group) {
                xEventGroupClearBits(s_event_group, WIFI_EVENT_CONNECTED);
                xEventGroupSetBits(s_event_group, WIFI_EVENT_DISCONNECTED);
            }
            schedule_reconnect();   /* Non-blocking */
            break;

        default:
            break;
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected — IP: " IPSTR " RSSI: %d",
                 IP2STR(&ev->ip_info.ip), wifi_manager_get_rssi());

        /* Stop any pending reconnect timer */
        xTimerStop(s_reconnect_tmr, 0);

        s_connected   = true;
        s_retry_count = 0;

        if (s_event_group) {
            xEventGroupClearBits(s_event_group, WIFI_EVENT_DISCONNECTED);
            xEventGroupSetBits(s_event_group, WIFI_EVENT_CONNECTED);
        }
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t wifi_manager_start(const char *ssid, const char *password)
{
    if (!ssid) { ESP_LOGE(TAG, "SSID is NULL"); return ESP_ERR_INVALID_ARG; }

    s_event_group = xEventGroupCreate();
    if (!s_event_group) return ESP_ERR_NO_MEM;

    /* Create the one-shot reconnect timer (initial period irrelevant — changed before use) */
    s_reconnect_tmr = xTimerCreate("wifi_reconnect", pdMS_TO_TICKS(1000),
                                    pdFALSE, NULL, reconnect_timer_cb);
    if (!s_reconnect_tmr) {
        ESP_LOGE(TAG, "Reconnect timer create failed");
        return ESP_ERR_NO_MEM;
    }

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

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    wifi_cfg.sta.ssid[sizeof(wifi_cfg.sta.ssid) - 1] = '\0';
    if (password) {
        strncpy((char *)wifi_cfg.sta.password, password,
                sizeof(wifi_cfg.sta.password) - 1);
        wifi_cfg.sta.password[sizeof(wifi_cfg.sta.password) - 1] = '\0';
    }
    wifi_cfg.sta.threshold.authmode =
        password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA),          TAG, "Set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "Set config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(),                           TAG, "WiFi start failed");

    ESP_LOGI(TAG, "WiFi STA starting — SSID: %s", ssid);
    return ESP_OK;
}

bool               wifi_manager_is_connected(void)      { return s_connected; }
EventGroupHandle_t wifi_manager_get_event_group(void)   { return s_event_group; }

int8_t wifi_manager_get_rssi(void)
{
    wifi_ap_record_t ap;
    return (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
}
