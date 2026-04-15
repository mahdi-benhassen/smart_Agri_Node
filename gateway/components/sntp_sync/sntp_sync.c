/*
 * sntp_sync.c — SNTP time synchronization with ISO-8601 formatting
 */
#include "sntp_sync.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <string.h>

static const char *TAG = "SNTP";
static bool s_synced = false;

static void sntp_callback(struct timeval *tv)
{
    s_synced = true;
    ESP_LOGI(TAG, "Time synchronized");
}

esp_err_t sntp_sync_start(const char *ntp_server)
{
    const char *server = ntp_server ? ntp_server : "pool.ntp.org";

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, server);
    sntp_set_time_sync_notification_cb(sntp_callback);
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP started — server: %s", server);

    /* Wait up to 15 seconds for initial sync */
    for (int i = 0; i < 15 && !s_synced; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (!s_synced) {
        ESP_LOGW(TAG, "Initial sync timeout (will retry in background)");
    }

    return ESP_OK;
}

esp_err_t sntp_sync_get_iso8601(char *buf, size_t len)
{
    if (!buf || len < 25) return ESP_ERR_INVALID_ARG;

    time_t now;
    struct tm timeinfo;
    time(&now);
    gmtime_r(&now, &timeinfo);

    strftime(buf, len, "%Y-%m-%dT%H:%M:%S.000Z", &timeinfo);
    return ESP_OK;
}

bool sntp_sync_is_synced(void) { return s_synced; }
