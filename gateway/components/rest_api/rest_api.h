/*
 * rest_api.h — HTTP REST server on port 8080
 */
#ifndef REST_API_H
#define REST_API_H
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t rest_api_start(void);
void      rest_api_stop(void);
#ifdef __cplusplus
}
#endif
#endif
