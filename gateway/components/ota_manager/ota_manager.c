/*
 * ota_manager.c — Gateway OTA via HTTPS + Node OTA orchestration
 *
 * FIXES applied:
 *  1. ota_check_task now actually parses the version-check JSON response.
 *  2. is_newer_version() is called and triggers a real OTA flash when newer.
 *  3. The binary download URL comes from the JSON "url" field, not the version
 *     endpoint URL (those are two different endpoints).
 */
#include "ota_manager.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_app_format.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "OTA_MGR";

static char s_ota_version_url[256] = "";   /* .../api/v1/version?target=gw */
static bool s_running = false;

#define FW_VERSION          "1.4.2"
#define HTTP_RX_BUF_SIZE    512

/* ── Internal: Compare semantic versions ───────────────────────────── */
static bool is_newer_version(const char *current, const char *available)
{
    int cur_major = 0, cur_minor = 0, cur_patch = 0;
    int avl_major = 0, avl_minor = 0, avl_patch = 0;

    sscanf(current,   "%d.%d.%d", &cur_major, &cur_minor, &cur_patch);
    sscanf(available, "%d.%d.%d", &avl_major, &avl_minor, &avl_patch);

    if (avl_major > cur_major) return true;
    if (avl_major == cur_major && avl_minor > cur_minor) return true;
    if (avl_major == cur_major && avl_minor == cur_minor && avl_patch > cur_patch) return true;
    return false;
}

/* ── Internal: Download and flash from a binary URL ────────────────── */
static esp_err_t perform_ota_from_url(const char *bin_url)
{
    ESP_LOGI(TAG, "Starting OTA flash from: %s", bin_url);

    esp_http_client_config_t http_cfg = {
        .url        = bin_url,
        .timeout_ms = 60000,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA succeeded — rebooting in 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA flash failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* ── OTA Check Task ────────────────────────────────────────────────── */
static void ota_check_task(void *arg)
{
    esp_task_wdt_add(NULL);

    while (s_running) {
        esp_task_wdt_reset();

        if (strlen(s_ota_version_url) == 0) {
            ESP_LOGW(TAG, "No OTA server URL configured — skipping check");
            vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_INTERVAL_SEC * 1000));
            continue;
        }

        ESP_LOGI(TAG, "Checking OTA: %s  (running v%s)", s_ota_version_url, FW_VERSION);

        /* ── Step 1: GET version endpoint ─────────────────────── */
        char rx_buf[HTTP_RX_BUF_SIZE] = {0};
        int  rx_len = 0;

        esp_http_client_config_t http_cfg = {
            .url        = s_ota_version_url,
            .timeout_ms = 10000,
        };

        esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
        if (!client) {
            ESP_LOGE(TAG, "HTTP client init failed");
            goto next_check;
        }

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "HTTP open failed: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            goto next_check;
        }

        esp_http_client_fetch_headers(client);
        rx_len = esp_http_client_read(client, rx_buf, sizeof(rx_buf) - 1);
        int status = esp_http_client_get_status_code(client);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        if (status != 200 || rx_len <= 0) {
            ESP_LOGW(TAG, "Version check failed: HTTP %d, %d bytes", status, rx_len);
            goto next_check;
        }

        rx_buf[rx_len] = '\0';
        ESP_LOGD(TAG, "Version response: %s", rx_buf);

        /* ── Step 2: Parse JSON ───────────────────────────────── */
        /*  Expected: {"target":"gw","version":"1.5.0","url":"http://..."} */
        {
            cJSON *root = cJSON_Parse(rx_buf);
            if (!root) {
                ESP_LOGW(TAG, "JSON parse failed");
                goto next_check;
            }

            cJSON *ver_item = cJSON_GetObjectItem(root, "version");
            cJSON *url_item = cJSON_GetObjectItem(root, "url");

            if (!cJSON_IsString(ver_item) || !cJSON_IsString(url_item)) {
                ESP_LOGW(TAG, "Malformed version JSON");
                cJSON_Delete(root);
                goto next_check;
            }

            const char *avail_version = ver_item->valuestring;
            const char *bin_url       = url_item->valuestring;

            /* ── Step 3: Compare and flash if newer ───────────── */
            if (is_newer_version(FW_VERSION, avail_version)) {
                ESP_LOGI(TAG, "New version available: %s → %s", FW_VERSION, avail_version);

                /* Copy URL before freeing JSON */
                char bin_url_copy[256];
                strncpy(bin_url_copy, bin_url, sizeof(bin_url_copy) - 1);
                bin_url_copy[sizeof(bin_url_copy) - 1] = '\0';

                cJSON_Delete(root);
                perform_ota_from_url(bin_url_copy);
                /* If OTA failed we fall through and retry next cycle */
            } else {
                ESP_LOGI(TAG, "Firmware up-to-date (v%s)", FW_VERSION);
                cJSON_Delete(root);
            }
        }

next_check:
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_INTERVAL_SEC * 1000));
    }

    vTaskDelete(NULL);
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t ota_manager_start(const char *ota_server_url)
{
    if (ota_server_url) {
        strncpy(s_ota_version_url, ota_server_url, sizeof(s_ota_version_url) - 1);
        s_ota_version_url[sizeof(s_ota_version_url) - 1] = '\0';
    }

    s_running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(ota_check_task, "ota_check", 8192,
                                              NULL, 2, NULL, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "OTA task create failed");
        return ESP_ERR_NO_MEM;
    }

    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "OTA manager started — project: %s  running: v%s",
             app->project_name, app->version);
    return ESP_OK;
}

esp_err_t ota_manager_check_update(void)
{
    if (strlen(s_ota_version_url) == 0) {
        ESP_LOGW(TAG, "No OTA server URL configured");
        return ESP_ERR_INVALID_STATE;
    }
    /* Trigger an immediate check by delegating to the task mechanism.
       In production this would signal the task via an EventGroup bit. */
    ESP_LOGI(TAG, "Manual OTA check triggered");
    return ESP_OK;
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
