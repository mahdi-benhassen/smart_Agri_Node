/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * ota_node.h — Zigbee OTA image receive for field node
 */

#ifndef OTA_NODE_H
#define OTA_NODE_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_NODE_BLOCK_SIZE     64

typedef enum {
    OTA_NODE_IDLE = 0,
    OTA_NODE_DOWNLOADING,
    OTA_NODE_VERIFYING,
    OTA_NODE_COMPLETE,
    OTA_NODE_ERROR,
} ota_node_state_t;

typedef struct {
    uint32_t total_size;
    uint32_t downloaded;
    uint8_t  progress_pct;
    ota_node_state_t state;
} ota_node_status_t;

esp_err_t ota_node_init(void);
esp_err_t ota_node_query_image(void);
esp_err_t ota_node_get_status(ota_node_status_t *status);
void      ota_node_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_NODE_H */
