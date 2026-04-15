/*
 * wifi_manager.h — Wi-Fi STA with auto-reconnect and exponential backoff
 */
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H
#include "esp_err.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_EVENT_CONNECTED    (1 << 0)
#define WIFI_EVENT_DISCONNECTED (1 << 1)
#define WIFI_MAX_RETRY          10
#define WIFI_BACKOFF_BASE_MS    1000
#define WIFI_BACKOFF_MAX_MS     60000

esp_err_t wifi_manager_start(const char *ssid, const char *password);
bool      wifi_manager_is_connected(void);
int8_t    wifi_manager_get_rssi(void);
EventGroupHandle_t wifi_manager_get_event_group(void);

#ifdef __cplusplus
}
#endif
#endif
