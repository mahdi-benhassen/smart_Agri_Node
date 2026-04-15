/*
 * cloud_buffer.h — SPIFFS ring-buffer for offline telemetry storage
 */
#ifndef CLOUD_BUFFER_H
#define CLOUD_BUFFER_H
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

#define CLOUD_BUFFER_MAX_ENTRIES  8640   /* 72h at 30s = 8640 entries */

esp_err_t cloud_buffer_init(void);
esp_err_t cloud_buffer_write(const char *json);
esp_err_t cloud_buffer_read_next(char *buf, size_t max_len);
bool      cloud_buffer_has_data(void);
uint32_t  cloud_buffer_count(void);
esp_err_t cloud_buffer_clear(void);

#ifdef __cplusplus
}
#endif
#endif
