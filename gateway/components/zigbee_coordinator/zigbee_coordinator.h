/*
 * zigbee_coordinator.h — Zigbee 3.0 Coordinator for gateway
 */
#ifndef ZIGBEE_COORDINATOR_H
#define ZIGBEE_COORDINATOR_H
#include "esp_err.h"
#include "agri_data_model.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZB_COORD_MAX_NODES  250

typedef void (*zb_telemetry_callback_t)(const agri_sensor_data_t *data,
                                         uint16_t short_addr);

esp_err_t zigbee_coordinator_start(void);
void      zigbee_coordinator_set_telemetry_cb(zb_telemetry_callback_t cb);
esp_err_t zigbee_coordinator_send_cmd(uint16_t short_addr, const agri_cmd_t *cmd);
esp_err_t zigbee_coordinator_permit_join(bool enable, uint8_t duration_sec);
int       zigbee_coordinator_get_node_count(void);
esp_err_t zigbee_coordinator_get_node_info(int index, agri_node_info_t *info);

#ifdef __cplusplus
}
#endif
#endif
