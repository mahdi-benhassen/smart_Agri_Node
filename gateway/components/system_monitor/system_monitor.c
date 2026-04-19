/*
 * system_monitor.c — Heap watchdog, uptime, periodic health logging
 *
 * FIX applied:
 *  15. sys_monitor_task now calls esp_task_wdt_add(NULL) for itself (was
 *      already present) AND the init function initialises the task watchdog
 *      with a panic action so that any task that stops feeding causes a
 *      visible crash+reboot rather than a silent hang.
 *
 *      All other long-running tasks (mqtt_pub_task, ota_check_task,
 *      zb_coord_task) must ALSO call esp_task_wdt_add(NULL) at their start
 *      and esp_task_wdt_reset() in their main loop — those calls are present
 *      in their respective fixed source files.
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
    /* Register this task with the task watchdog */
    esp_task_wdt_add(NULL);

    while (1) {
        uint32_t heap     = esp_get_free_heap_size();
        uint32_t min_heap = esp_get_minimum_free_heap_size();
        uint32_t uptime   = system_monitor_get_uptime_sec();

        ESP_LOGI(TAG, "Heap: %lu B (min: %lu B) | Uptime: %lu s",
                 (unsigned long)heap, (unsigned long)min_heap, (unsigned long)uptime);

        if (heap < HEAP_LOW_THRESHOLD) {
            ESP_LOGW(TAG, "LOW HEAP: %lu bytes", (unsigned long)heap);
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }
}

esp_err_t system_monitor_init(void)
{
    s_start_time_us = esp_timer_get_time();

    /*
     * Initialise the task watchdog daemon with a 30 s timeout and panic
     * enabled.  If CONFIG_ESP_TASK_WDT is set in sdkconfig.defaults the
     * daemon is already started by the bootloader, but reconfiguring here
     * ensures the timeout matches our sdkconfig value and panic is active.
     *
     * Every task that calls esp_task_wdt_add(NULL) must also call
     * esp_task_wdt_reset() at least once per CONFIG_ESP_TASK_WDT_TIMEOUT_S
     * seconds, or the system will reboot.
     */
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms     = 30000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic  = true,
    };
    esp_err_t ret = esp_task_wdt_reconfigure(&wdt_cfg);
    if (ret != ESP_OK && ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Task WDT reconfigure: %s", esp_err_to_name(ret));
    }

    ret = xTaskCreatePinnedToCore(sys_monitor_task, "sys_mon", 2048,
                                   NULL, 1, NULL, 0) == pdPASS
          ? ESP_OK : ESP_ERR_NO_MEM;
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sys_monitor_task create failed");
        return ret;
    }

    ESP_LOGI(TAG, "System monitor started (WDT timeout=30s, panic=true)");
    return ESP_OK;
}

uint32_t system_monitor_get_free_heap(void)  { return esp_get_free_heap_size(); }
uint32_t system_monitor_get_min_heap(void)   { return esp_get_minimum_free_heap_size(); }
uint32_t system_monitor_get_uptime_sec(void) {
    return (uint32_t)((esp_timer_get_time() - s_start_time_us) / 1000000ULL);
}
