/*
 * system_monitor.h — Heap, task watchdog, RSSI monitoring for gateway
 */
#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t system_monitor_init(void);
uint32_t  system_monitor_get_free_heap(void);
uint32_t  system_monitor_get_min_heap(void);
uint32_t  system_monitor_get_uptime_sec(void);
#ifdef __cplusplus
}
#endif
#endif
