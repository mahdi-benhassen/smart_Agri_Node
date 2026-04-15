/*
 * mqtt_client.h — MQTT 3.1.1 over TLS pub/sub client
 */
#ifndef MQTT_CLIENT_MGR_H
#define MQTT_CLIENT_MGR_H
#include "esp_err.h"
#include "agri_data_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mqtt_cmd_callback_t)(const agri_cmd_t *cmd);

esp_err_t mqtt_client_start(const char *broker_uri, const char *username,
                             const char *password, const char *farm_id);
esp_err_t mqtt_client_publish_telemetry(const char *node_id,
                                         const agri_sensor_data_t *data);
esp_err_t mqtt_client_publish_status(const char *node_id, const char *status);
esp_err_t mqtt_client_publish_health(void);
void      mqtt_client_set_cmd_callback(mqtt_cmd_callback_t cb);
bool      mqtt_client_is_connected(void);
void      mqtt_client_stop(void);

#ifdef __cplusplus
}
#endif
#endif
