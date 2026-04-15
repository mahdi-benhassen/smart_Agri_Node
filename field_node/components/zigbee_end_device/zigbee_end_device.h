/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * zigbee_end_device.h — Zigbee 3.0 End Device for field sensor node
 */

#ifndef ZIGBEE_END_DEVICE_H
#define ZIGBEE_END_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "agri_data_model.h"
#include "agri_zigbee_clusters.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZB_ED_JOIN_TIMEOUT_SEC      60
#define ZB_ED_REPORT_TIMEOUT_MS     5000
#define ZB_ED_REPORT_MAX_RETRIES    3
#define ZB_ED_POLL_INTERVAL_MS      3000
#define ZB_ED_OTA_CHECK_INTERVAL    24      /* Every 24 wakeups ≈ 12 min */

typedef enum {
    ZB_ED_STATE_IDLE = 0,
    ZB_ED_STATE_JOINING,
    ZB_ED_STATE_JOINED,
    ZB_ED_STATE_REPORTING,
    ZB_ED_STATE_OTA,
    ZB_ED_STATE_ERROR,
} zb_ed_state_t;

typedef struct {
    uint8_t   channel;              /* 0 = auto-scan */
    uint16_t  pan_id;               /* 0 = auto */
    bool      install_code_enabled;
} zb_ed_config_t;

/**
 * @brief Initialize Zigbee End Device stack.
 */
esp_err_t zigbee_ed_init(const zb_ed_config_t *config);

/**
 * @brief Start Zigbee stack (call after init).
 * This is blocking until the stack is ready or join times out.
 */
esp_err_t zigbee_ed_start(void);

/**
 * @brief Check if device is joined to a Zigbee network.
 */
bool zigbee_ed_is_joined(void);

/**
 * @brief Report all sensor data to coordinator via AGRI_CLUSTER attributes.
 *
 * @param data  Sensor data to report
 * @return ESP_OK on ACK received, ESP_ERR_TIMEOUT on no ACK after retries
 */
esp_err_t zigbee_ed_report_data(const agri_sensor_data_t *data);

/**
 * @brief Check for pending actuator commands from coordinator.
 *
 * @param cmd   Output command (if available)
 * @return ESP_OK if command received, ESP_ERR_NOT_FOUND if none
 */
esp_err_t zigbee_ed_poll_command(agri_cmd_t *cmd);

/**
 * @brief Check OTA status via Zigbee OTA cluster.
 *
 * @return ESP_OK if OTA available, ESP_ERR_NOT_FOUND if no update
 */
esp_err_t zigbee_ed_check_ota(void);

/**
 * @brief Get current Zigbee state.
 */
zb_ed_state_t zigbee_ed_get_state(void);

/**
 * @brief Get RSSI of last communication.
 */
int8_t zigbee_ed_get_rssi(void);

/**
 * @brief Get LQI of last communication.
 */
uint8_t zigbee_ed_get_lqi(void);

/**
 * @brief Get short address assigned by coordinator.
 */
uint16_t zigbee_ed_get_short_addr(void);

/**
 * @brief Check if Zigbee TX is pending (must complete before sleep).
 */
bool zigbee_ed_is_tx_pending(void);

/**
 * @brief Deinitialize Zigbee stack.
 */
void zigbee_ed_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* ZIGBEE_END_DEVICE_H */
