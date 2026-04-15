/*
 * sntp_sync.h — SNTP time synchronization
 */
#ifndef SNTP_SYNC_H
#define SNTP_SYNC_H
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t sntp_sync_start(const char *ntp_server);
esp_err_t sntp_sync_get_iso8601(char *buf, size_t len);
bool      sntp_sync_is_synced(void);
#ifdef __cplusplus
}
#endif
#endif
