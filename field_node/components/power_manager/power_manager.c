/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * power_manager.c — Deep-sleep scheduling and battery monitoring
 */

#include "power_manager.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "POWER_MGR";

static power_config_t s_config;
static adc_oneshot_unit_handle_t s_batt_adc = NULL;
static bool s_initialized = false;

/* RTC memory — persists across deep sleep */
static RTC_DATA_ATTR uint32_t s_boot_count = 0;

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t power_manager_init(const power_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(power_config_t));
    s_boot_count++;

    /* Set defaults */
    if (s_config.sleep_duration_sec == 0) {
        s_config.sleep_duration_sec = POWER_DEFAULT_SLEEP_SEC;
    }
    if (s_config.low_batt_threshold_mv == 0) {
        s_config.low_batt_threshold_mv = POWER_LOW_BATT_MV;
    }
    if (s_config.critical_batt_threshold_mv == 0) {
        s_config.critical_batt_threshold_mv = POWER_CRITICAL_BATT_MV;
    }
    if (s_config.batt_divider_ratio == 0.0f) {
        s_config.batt_divider_ratio = 2.0f;  /* Typical 1:1 divider */
    }

    /* Initialize battery ADC */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = s_config.batt_adc_unit,
    };

    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &s_batt_adc);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Battery ADC init failed: %s (continuing without battery monitoring)",
                 esp_err_to_name(ret));
        s_batt_adc = NULL;
    } else {
        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten    = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ret = adc_oneshot_config_channel(s_batt_adc, s_config.batt_adc_channel, &chan_cfg);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Battery ADC channel config failed: %s", esp_err_to_name(ret));
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Initialized — boot #%lu, sleep=%lus, wakeup=%d",
             (unsigned long)s_boot_count,
             (unsigned long)s_config.sleep_duration_sec,
             (int)power_manager_get_wakeup_reason());
    return ESP_OK;
}

wakeup_reason_t power_manager_get_wakeup_reason(void)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
        return WAKEUP_REASON_TIMER;
    case ESP_SLEEP_WAKEUP_GPIO:
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:
        return WAKEUP_REASON_GPIO;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        return WAKEUP_REASON_RESET;
    default:
        return WAKEUP_REASON_OTHER;
    }
}

uint16_t power_manager_read_battery_mv(void)
{
    if (s_batt_adc == NULL) {
        return 0;
    }

    /* Take multiple samples and average */
    int32_t sum = 0;
    int valid = 0;

    for (int i = 0; i < 5; i++) {
        int raw = 0;
        esp_err_t ret = adc_oneshot_read(s_batt_adc, s_config.batt_adc_channel, &raw);
        if (ret == ESP_OK) {
            sum += raw;
            valid++;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    if (valid == 0) return 0;

    int avg_raw = (int)(sum / valid);

    /* Convert: ADC 12-bit, 3.3V ref, apply voltage divider ratio */
    float voltage_v = ((float)avg_raw / 4095.0f) * 3.3f * s_config.batt_divider_ratio;
    uint16_t mv = (uint16_t)(voltage_v * 1000.0f);

    ESP_LOGD(TAG, "Battery: raw=%d → %u mV", avg_raw, mv);
    return mv;
}

bool power_manager_is_low_battery(void)
{
    uint16_t mv = power_manager_read_battery_mv();
    if (mv == 0) return false;
    return (mv < s_config.low_batt_threshold_mv);
}

bool power_manager_is_critical_battery(void)
{
    uint16_t mv = power_manager_read_battery_mv();
    if (mv == 0) return false;
    return (mv < s_config.critical_batt_threshold_mv);
}

void power_manager_enter_deep_sleep(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized — using default sleep time");
    }

    uint64_t sleep_us = (uint64_t)s_config.sleep_duration_sec * 1000000ULL;
    ESP_LOGI(TAG, "Entering deep sleep for %lu seconds (boot #%lu)",
             (unsigned long)s_config.sleep_duration_sec, (unsigned long)s_boot_count);

    esp_sleep_enable_timer_wakeup(sleep_us);

    /* Flush logs before sleep */
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_deep_sleep_start();
    /* Execution never reaches here */
}

void power_manager_set_sleep_duration(uint32_t seconds)
{
    if (seconds == 0) seconds = POWER_DEFAULT_SLEEP_SEC;
    s_config.sleep_duration_sec = seconds;
    ESP_LOGI(TAG, "Sleep duration updated: %lu s", (unsigned long)seconds);
}

uint32_t power_manager_get_boot_count(void)
{
    return s_boot_count;
}

void power_manager_deinit(void)
{
    if (s_batt_adc != NULL) {
        adc_oneshot_del_unit(s_batt_adc);
        s_batt_adc = NULL;
    }
    s_initialized = false;
}
