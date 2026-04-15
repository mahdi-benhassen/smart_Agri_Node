/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * ota_node.c — Zigbee OTA receive for field node (via OTA cluster 0x0019)
 */

#include "ota_node.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "OTA_NODE";

static ota_node_state_t s_state = OTA_NODE_IDLE;
static uint32_t s_total_size = 0;
static uint32_t s_downloaded = 0;
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_update_partition = NULL;
static bool s_initialized = false;

esp_err_t ota_node_init(void)
{
    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (s_update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA update partition found");
        return ESP_ERR_NOT_FOUND;
    }

    s_state = OTA_NODE_IDLE;
    s_total_size = 0;
    s_downloaded = 0;
    s_initialized = true;

    ESP_LOGI(TAG, "Initialized — update partition: %s (offset=0x%lX, size=0x%lX)",
             s_update_partition->label,
             (unsigned long)s_update_partition->address,
             (unsigned long)s_update_partition->size);
    return ESP_OK;
}

esp_err_t ota_node_query_image(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* In a real implementation, this would send a Zigbee OTA Query Next Image
       request to the coordinator acting as OTA server. The coordinator would
       respond with image availability, size, and version info.

       The actual data transfer uses OTA Image Block Request/Response frames,
       each carrying OTA_NODE_BLOCK_SIZE bytes. The node resumes from the
       last downloaded block after sleep/wake cycles. */

    ESP_LOGI(TAG, "OTA query sent to coordinator");

    /* Placeholder — real implementation needs Zigbee OTA cluster integration */
    s_state = OTA_NODE_IDLE;
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ota_node_get_status(ota_node_status_t *status)
{
    if (status == NULL) return ESP_ERR_INVALID_ARG;

    status->state        = s_state;
    status->total_size   = s_total_size;
    status->downloaded   = s_downloaded;
    status->progress_pct = (s_total_size > 0)
                           ? (uint8_t)((s_downloaded * 100UL) / s_total_size)
                           : 0;
    return ESP_OK;
}

void ota_node_deinit(void)
{
    s_initialized = false;
    s_state = OTA_NODE_IDLE;
    ESP_LOGI(TAG, "Deinitialized");
}
