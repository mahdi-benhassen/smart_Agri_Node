/*
 * ota_manager.c — Gateway OTA via HTTPS + Node OTA orchestration
 */
#include "ota_manager.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_app_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "OTA_MGR";

static char s_ota_url[256] = "";
static bool s_running = false;

#define FW_VERSION  "1.4.2"

/* ── Internal: Compare semantic versions ───────────────────────────── */
static bool is_newer_version(const char *current, const char *available)
{
    int cur_major = 0, cur_minor = 0, cur_patch = 0;
    int avl_major = 0, avl_minor = 0, avl_patch = 0;

    sscanf(current, "%d.%d.%d", &cur_major, &cur_minor, &cur_patch);
    sscanf(available, "%d.%d.%d", &avl_major, &avl_minor, &avl_patch);

    if (avl_major > cur_major) return true;
    if (avl_major == cur_major && avl_minor > cur_minor) return true;
    if (avl_major == cur_major && avl_minor == cur_minor && avl_patch > cur_patch) return true;
    return false;
}

/* ── OTA Check Task ────────────────────────────────────────────────── */
static void ota_check_task(void *arg)
{
    while (s_running) {
        ESP_LOGI(TAG, "Checking for OTA update...");

        /* In production, this would:
           1. HTTP GET ${ota_url}/api/v1/version?target=gw
           2. Parse JSON response for available version
           3. Compare with FW_VERSION
           4. If newer, download via esp_https_ota() to standby partition
           5. Verify SHA-256 + RSA-2048 signature
           6. Schedule reboot in 30 seconds */

        if (strlen(s_ota_url) > 0) {
            ESP_LOGI(TAG, "OTA server: %s — current version: %s", s_ota_url, FW_VERSION);

            /* Check version endpoint */
            esp_http_client_config_t http_cfg = {
                .url = s_ota_url,
                .timeout_ms = 10000,
            };

            esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
            if (client) {
                esp_err_t err = esp_http_client_perform(client);
                if (err == ESP_OK) {
                    int status = esp_http_client_get_status_code(client);
                    ESP_LOGI(TAG, "Version check HTTP %d", status);
                }
                esp_http_client_cleanup(client);
            }
        }

        /* Wait for next check interval */
        vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_INTERVAL_SEC * 1000));
    }

    vTaskDelete(NULL);
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t ota_manager_start(const char *ota_server_url)
{
    if (ota_server_url) {
        strncpy(s_ota_url, ota_server_url, sizeof(s_ota_url) - 1);
    }

    s_running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(ota_check_task, "ota_check", 8192,
                                              NULL, 2, NULL, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "OTA task create failed");
        return ESP_ERR_NO_MEM;
    }

    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "OTA manager started — running: %s v%s", app->project_name, app->version);
    return ESP_OK;
}

esp_err_t ota_manager_check_update(void)
{
    if (strlen(s_ota_url) == 0) {
        ESP_LOGW(TAG, "No OTA server URL configured");
        return ESP_ERR_INVALID_STATE;
    }

    esp_http_client_config_t config = {
        .url = s_ota_url,
        .timeout_ms = 30000,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    ESP_LOGI(TAG, "Starting OTA from: %s", s_ota_url);
    esp_err_t ret = esp_https_ota(&ota_config);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA succeeded — rebooting in 30 seconds...");
        vTaskDelay(pdMS_TO_TICKS(30000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t ota_manager_trigger_node_ota(uint16_t node_addr, const char *image_url)
{
    if (!image_url) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Triggering node OTA: addr=0x%04X url=%s", node_addr, image_url);

    /* In production:
       1. Download image from URL to PSRAM/temp SPIFFS file
       2. Activate as Zigbee OTA Upgrade Server (cluster 0x0019)
       3. Node queries on next wakeup, receives image blocks
       4. Node verifies SHA-256 and reboots */

    return ESP_OK;
}
