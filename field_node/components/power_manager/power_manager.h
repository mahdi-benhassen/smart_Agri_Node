/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * power_manager.h — Deep-sleep scheduling and battery monitoring
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POWER_DEFAULT_SLEEP_SEC     30
#define POWER_LOW_BATT_MV           3000    /* Low battery threshold */
#define POWER_CRITICAL_BATT_MV      2800    /* Critical — skip sensors */

typedef enum {
    WAKEUP_REASON_TIMER  = 0,
    WAKEUP_REASON_GPIO   = 1,
    WAKEUP_REASON_RESET  = 2,
    WAKEUP_REASON_OTHER  = 3,
} wakeup_reason_t;

typedef struct {
    uint32_t sleep_duration_sec;        /* Deep-sleep duration */
    uint16_t low_batt_threshold_mv;     /* Low battery warning */
    uint16_t critical_batt_threshold_mv;
    adc_channel_t batt_adc_channel;     /* ADC channel for battery */
    adc_unit_t    batt_adc_unit;
    float  batt_divider_ratio;          /* Voltage divider ratio */
} power_config_t;

esp_err_t power_manager_init(const power_config_t *config);
wakeup_reason_t power_manager_get_wakeup_reason(void);
uint16_t power_manager_read_battery_mv(void);
bool     power_manager_is_low_battery(void);
bool     power_manager_is_critical_battery(void);
void     power_manager_enter_deep_sleep(void);
void     power_manager_set_sleep_duration(uint32_t seconds);
uint32_t power_manager_get_boot_count(void);
void     power_manager_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MANAGER_H */
