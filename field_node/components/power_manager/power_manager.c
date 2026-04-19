/*
 * power_manager.c — Deep-sleep scheduling and battery monitoring
 *
 * FIX applied:
 *  9. Battery ADC now uses the ESP-IDF esp_adc/adc_cali_* calibration API
 *     (curve fitting or line fitting, selected at runtime) to correct for the
 *     ADC's nonlinearity.  Without calibration readings can be off by up to
 *     10%, which matters when the low-battery threshold is 3000 mV.
 */
#include "power_manager.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "POWER_MGR";

static power_config_t              s_config;
static adc_oneshot_unit_handle_t   s_batt_adc  = NULL;
static adc_cali_handle_t           s_batt_cali = NULL;
static bool                        s_cali_ok   = false;
static bool                        s_initialized = false;

static RTC_DATA_ATTR uint32_t s_boot_count = 0;

/* ── Internal: Initialise ADC calibration ──────────────────────────── */
static void adc_cali_init(adc_unit_t unit, adc_channel_t channel,
                           adc_atten_t atten)
{
    /* Try curve-fitting scheme first (available on ESP32-S3 / H2) */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = unit,
        .chan     = channel,
        .atten    = atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_batt_cali) == ESP_OK) {
        s_cali_ok = true;
        ESP_LOGI(TAG, "ADC calibration: curve-fitting");
        return;
    }
#endif

    /* Fall back to line-fitting */
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t lf_cfg = {
        .unit_id  = unit,
        .atten    = atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_line_fitting(&lf_cfg, &s_batt_cali) == ESP_OK) {
        s_cali_ok = true;
        ESP_LOGI(TAG, "ADC calibration: line-fitting");
        return;
    }
#endif

    ESP_LOGW(TAG, "No ADC calibration scheme available — raw readings used");
    s_cali_ok = false;
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t power_manager_init(const power_config_t *config)
{
    if (!config) { ESP_LOGE(TAG, "NULL config"); return ESP_ERR_INVALID_ARG; }

    memcpy(&s_config, config, sizeof(power_config_t));
    s_boot_count++;

    if (s_config.sleep_duration_sec == 0)         s_config.sleep_duration_sec         = POWER_DEFAULT_SLEEP_SEC;
    if (s_config.low_batt_threshold_mv == 0)       s_config.low_batt_threshold_mv       = POWER_LOW_BATT_MV;
    if (s_config.critical_batt_threshold_mv == 0)  s_config.critical_batt_threshold_mv  = POWER_CRITICAL_BATT_MV;
    if (s_config.batt_divider_ratio == 0.0f)       s_config.batt_divider_ratio          = 2.0f;

    /* Initialise ADC oneshot driver */
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = s_config.batt_adc_unit };
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &s_batt_adc);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Battery ADC unit init failed: %s", esp_err_to_name(ret));
        s_batt_adc = NULL;
    } else {
        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten    = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ret = adc_oneshot_config_channel(s_batt_adc,
                                          s_config.batt_adc_channel, &chan_cfg);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Battery ADC channel config failed: %s", esp_err_to_name(ret));
        } else {
            /* Initialise calibration for this unit/channel/attenuation */
            adc_cali_init(s_config.batt_adc_unit,
                          s_config.batt_adc_channel,
                          ADC_ATTEN_DB_12);
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Initialized — boot #%lu  sleep=%lus  wakeup=%d  cali=%s",
             (unsigned long)s_boot_count,
             (unsigned long)s_config.sleep_duration_sec,
             (int)power_manager_get_wakeup_reason(),
             s_cali_ok ? "yes" : "no");
    return ESP_OK;
}

wakeup_reason_t power_manager_get_wakeup_reason(void)
{
    switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER:  return WAKEUP_REASON_TIMER;
    case ESP_SLEEP_WAKEUP_GPIO:
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:   return WAKEUP_REASON_GPIO;
    case ESP_SLEEP_WAKEUP_UNDEFINED: return WAKEUP_REASON_RESET;
    default:                      return WAKEUP_REASON_OTHER;
    }
}

uint16_t power_manager_read_battery_mv(void)
{
    if (!s_batt_adc) return 0;

    int32_t sum   = 0;
    int     valid = 0;

    for (int i = 0; i < 5; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_batt_adc, s_config.batt_adc_channel, &raw) == ESP_OK) {
            sum += raw;
            valid++;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (valid == 0) return 0;

    int avg_raw = (int)(sum / valid);
    uint16_t mv = 0;

    if (s_cali_ok && s_batt_cali) {
        /* FIX 9: calibrated millivolt conversion */
        int cali_mv = 0;
        if (adc_cali_raw_to_voltage(s_batt_cali, avg_raw, &cali_mv) == ESP_OK) {
            /* Apply voltage-divider ratio */
            mv = (uint16_t)((float)cali_mv * s_config.batt_divider_ratio);
        } else {
            /* Fallback to raw formula on calibration read failure */
            float v = ((float)avg_raw / 4095.0f) * 3.3f * s_config.batt_divider_ratio;
            mv = (uint16_t)(v * 1000.0f);
        }
    } else {
        float v = ((float)avg_raw / 4095.0f) * 3.3f * s_config.batt_divider_ratio;
        mv = (uint16_t)(v * 1000.0f);
    }

    ESP_LOGD(TAG, "Battery: raw=%d → %u mV", avg_raw, mv);
    return mv;
}

bool power_manager_is_low_battery(void)
{
    uint16_t mv = power_manager_read_battery_mv();
    return (mv > 0 && mv < s_config.low_batt_threshold_mv);
}

bool power_manager_is_critical_battery(void)
{
    uint16_t mv = power_manager_read_battery_mv();
    return (mv > 0 && mv < s_config.critical_batt_threshold_mv);
}

void power_manager_enter_deep_sleep(void)
{
    uint64_t sleep_us = (uint64_t)s_config.sleep_duration_sec * 1000000ULL;
    ESP_LOGI(TAG, "Entering deep sleep for %lu seconds (boot #%lu)",
             (unsigned long)s_config.sleep_duration_sec,
             (unsigned long)s_boot_count);

    esp_sleep_enable_timer_wakeup(sleep_us);
    vTaskDelay(pdMS_TO_TICKS(100));     /* Flush log buffers */
    esp_deep_sleep_start();
}

void power_manager_set_sleep_duration(uint32_t seconds)
{
    if (seconds == 0) seconds = POWER_DEFAULT_SLEEP_SEC;
    s_config.sleep_duration_sec = seconds;
}

uint32_t power_manager_get_boot_count(void) { return s_boot_count; }

void power_manager_deinit(void)
{
    if (s_batt_cali) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(s_batt_cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(s_batt_cali);
#endif
        s_batt_cali = NULL;
    }
    if (s_batt_adc) {
        adc_oneshot_del_unit(s_batt_adc);
        s_batt_adc = NULL;
    }
    s_initialized = false;
}
