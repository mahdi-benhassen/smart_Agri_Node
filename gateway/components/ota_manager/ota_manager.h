/*
 * ota_manager.h — Gateway OTA (HTTPS A/B) + Node OTA (Zigbee) orchestration
 */
#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

#define OTA_CHECK_INTERVAL_SEC  (6 * 3600)  /* 6 hours */

esp_err_t ota_manager_start(const char *ota_server_url);
esp_err_t ota_manager_check_update(void);
esp_err_t ota_manager_trigger_node_ota(uint16_t node_addr, const char *image_url);

#ifdef __cplusplus
}
#endif
#endif
