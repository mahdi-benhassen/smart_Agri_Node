/*
 * system_monitor.c — Heap watchdog, uptime, periodic health logging
 */
#include "system_monitor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SYS_MON";
static int64_t s_start_time_us = 0;

#define MONITOR_INTERVAL_MS  30000
#define HEAP_LOW_THRESHOLD   16384

static void sys_monitor_task(void *arg)
{
    esp_task_wdt_add(NULL);
    while (1) {
        uint32_t heap = esp_get_free_heap_size();
        uint32_t min_heap = esp_get_minimum_free_heap_size();
        uint32_t uptime = system_monitor_get_uptime_sec();

        ESP_LOGI(TAG, "Heap: %lu / min: %lu | Uptime: %lu s",
                 (unsigned long)heap, (unsigned long)min_heap, (unsigned long)uptime);

        if (heap < HEAP_LOW_THRESHOLD) {
            ESP_LOGW(TAG, "⚠ LOW HEAP: %lu bytes", (unsigned long)heap);
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }
}

esp_err_t system_monitor_init(void)
{
    s_start_time_us = esp_timer_get_time();

    BaseType_t ret = xTaskCreatePinnedToCore(sys_monitor_task, "sys_mon", 2048,
                                              NULL, 1, NULL, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "System monitor started");
    return ESP_OK;
}

uint32_t system_monitor_get_free_heap(void)  { return esp_get_free_heap_size(); }
uint32_t system_monitor_get_min_heap(void)   { return esp_get_minimum_free_heap_size(); }
uint32_t system_monitor_get_uptime_sec(void) {
    return (uint32_t)((esp_timer_get_time() - s_start_time_us) / 1000000ULL);
}
