/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * Field Node — main.c
 * ESP32-H2 Zigbee End Device: sensor acquisition → data processing →
 * Zigbee report → actuator poll → OTA check → deep sleep
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "nvs_config.h"
#include "agri_data_model.h"
#include "sensor_hub.h"
#include "actuator_controller.h"
#include "zigbee_end_device.h"
#include "power_manager.h"
#include "rain_gauge_driver.h"
#include "ota_node.h"

static const char *TAG = "FIELD_NODE";

#define FW_VERSION  "1.4.2"

/* Wakeup counter for OTA check scheduling */
static RTC_DATA_ATTR uint32_t s_wakeup_count = 0;

/* ── GPIO Pin Defaults (ESP32-H2) ──────────────────────────────────── */
#define PIN_I2C_SDA         GPIO_NUM_1
#define PIN_I2C_SCL         GPIO_NUM_2
#define PIN_DHT22           GPIO_NUM_3
#define PIN_NPK_TX          GPIO_NUM_4
#define PIN_NPK_RX          GPIO_NUM_5
#define PIN_NPK_DE_RE       GPIO_NUM_8
#define PIN_RAIN_GAUGE      GPIO_NUM_9
#define PIN_VALVE_RELAY     GPIO_NUM_10
#define PIN_PUMP_PWM        GPIO_NUM_11
#define PIN_FAN_PWM         GPIO_NUM_12
#define PIN_LED_R           GPIO_NUM_25
#define PIN_LED_B           GPIO_NUM_26

/* ── State Machine ─────────────────────────────────────────────────── */

typedef enum {
    STATE_BOOT_INIT = 0,
    STATE_SENSOR_ACQUIRE,
    STATE_DATA_PROCESS,
    STATE_ZIGBEE_JOIN_CHECK,
    STATE_ZIGBEE_REPORT,
    STATE_ACTUATOR_POLL,
    STATE_OTA_CHECK,
    STATE_SLEEP_PREP,
    STATE_DEEP_SLEEP,
} node_state_t;

void app_main(void)
{
    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  Smart-Agri Field Node v%s", FW_VERSION);
    ESP_LOGI(TAG, "  Boot #%lu  Wakeup #%lu",
             (unsigned long)power_manager_get_boot_count(),
             (unsigned long)s_wakeup_count);
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    node_state_t state = STATE_BOOT_INIT;
    agri_sensor_data_t sensor_data;
    uint16_t alarm_flags = 0;

    while (1) {
        switch (state) {

        /* ── BOOT_INIT ─────────────────────────────────────────── */
        case STATE_BOOT_INIT: {
            ESP_LOGI(TAG, "[STATE] BOOT_INIT");

            /* Initialize NVS and load config */
            esp_err_t ret = nvs_config_init();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
                alarm_flags |= ALARM_FLAG_NVS_FAULT;
            }
            nvs_config_load();

            /* Initialize data struct */
            agri_data_init_defaults(&sensor_data);
            nvs_config_get_str(NVS_KEY_DEVICE_ID, sensor_data.device_id,
                                sizeof(sensor_data.device_id));

            /* Initialize power manager */
            uint32_t sleep_sec = 0;
            nvs_config_get_u32(NVS_KEY_SLEEP_SEC, &sleep_sec, 30);

            power_config_t pwr_cfg = {
                .sleep_duration_sec          = sleep_sec,
                .low_batt_threshold_mv       = 3000,
                .critical_batt_threshold_mv  = 2800,
                .batt_adc_channel            = ADC_CHANNEL_0,
                .batt_adc_unit               = ADC_UNIT_1,
                .batt_divider_ratio          = 2.0f,
            };
            power_manager_init(&pwr_cfg);

            /* Check battery */
            sensor_data.batt_mv = power_manager_read_battery_mv();
            if (power_manager_is_low_battery()) {
                alarm_flags |= ALARM_FLAG_LOW_BATTERY;
                ESP_LOGW(TAG, "LOW BATTERY: %u mV", sensor_data.batt_mv);
            }

            if (power_manager_is_critical_battery()) {
                ESP_LOGE(TAG, "CRITICAL BATTERY — skipping sensors, going to sleep");
                state = STATE_SLEEP_PREP;
                break;
            }

            /* Load calibration */
            agri_calibration_t cal;
            nvs_config_load_calibration(&cal);

            /* Initialize sensors */
            sensor_hub_config_t sh_cfg = {
                .i2c_port          = I2C_NUM_0,
                .i2c_sda           = PIN_I2C_SDA,
                .i2c_scl           = PIN_I2C_SCL,
                .i2c_freq_hz       = 100000,
                .dht22_pin         = PIN_DHT22,
                .soil_adc_unit     = ADC_UNIT_1,
                .soil_adc_channel  = ADC_CHANNEL_1,
                .soil_temp_channel = ADC_CHANNEL_2,
                .npk_uart_port     = UART_NUM_1,
                .npk_tx_pin        = PIN_NPK_TX,
                .npk_rx_pin        = PIN_NPK_RX,
                .npk_de_re_pin     = PIN_NPK_DE_RE,
                .rain_pin          = PIN_RAIN_GAUGE,
                .calibration       = cal,
            };
            nvs_config_get_u16(NVS_KEY_SOIL_CAL_DRY, &sh_cfg.soil_cal_dry, 3500);
            nvs_config_get_u16(NVS_KEY_SOIL_CAL_WET, &sh_cfg.soil_cal_wet, 1500);

            sensor_hub_init(&sh_cfg);

            /* Restore rain counter from NVS */
            uint32_t rain_count = 0;
            nvs_config_load_rain_count(&rain_count);
            rain_gauge_set_count(rain_count);

            /* Initialize actuators */
            actuator_config_t act_cfg = {
                .valve_pin         = PIN_VALVE_RELAY,
                .valve_feedback_pin = GPIO_NUM_NC,
                .pump_pin          = PIN_PUMP_PWM,
                .fan_pin           = PIN_FAN_PWM,
                .led_r_pin         = PIN_LED_R,
                .led_b_pin         = PIN_LED_B,
                .led_g_pin         = GPIO_NUM_NC,
                .led_w_pin         = GPIO_NUM_NC,
                .fert_pump_pin     = GPIO_NUM_NC,
                .fert_flow_pin     = GPIO_NUM_NC,
            };
            nvs_config_get_float(NVS_KEY_FAN_KP, &act_cfg.fan_pid_kp, 5.0f);
            nvs_config_get_float(NVS_KEY_FAN_KI, &act_cfg.fan_pid_ki, 0.5f);
            nvs_config_get_float(NVS_KEY_FAN_KD, &act_cfg.fan_pid_kd, 1.0f);
            nvs_config_get_float(NVS_KEY_FAN_SETPOINT, &act_cfg.fan_temp_setpoint, 25.0f);
            actuator_init(&act_cfg);

            state = STATE_SENSOR_ACQUIRE;
            break;
        }

        /* ── SENSOR_ACQUIRE ────────────────────────────────────── */
        case STATE_SENSOR_ACQUIRE: {
            ESP_LOGI(TAG, "[STATE] SENSOR_ACQUIRE");

            esp_err_t ret = sensor_hub_acquire_all(&sensor_data, &alarm_flags);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Sensor acquisition incomplete: %s", esp_err_to_name(ret));
            }

            state = STATE_DATA_PROCESS;
            break;
        }

        /* ── DATA_PROCESS ──────────────────────────────────────── */
        case STATE_DATA_PROCESS: {
            ESP_LOGI(TAG, "[STATE] DATA_PROCESS");

            /* Update actuator states in data struct */
            sensor_data.valve_open = actuator_get_valve_state();
            sensor_data.pump_pct   = actuator_get_pump_pct();
            sensor_data.fan_pct    = actuator_get_fan_pct();
            sensor_data.led_r_pct  = actuator_get_led_r_pct();
            sensor_data.led_b_pct  = actuator_get_led_b_pct();

            /* Update system health */
            sensor_data.batt_mv    = power_manager_read_battery_mv();
            sensor_data.alarm_flags = alarm_flags;

            /* Run fan PID controller */
            actuator_fan_pid_update(sensor_data.temp_c);
            sensor_data.fan_pct = actuator_get_fan_pct();

            ESP_LOGI(TAG, "Data: T=%.1f°C H=%.1f%% SM=%.1f%% CO2=%u Batt=%umV",
                     sensor_data.temp_c, sensor_data.humidity_pct,
                     sensor_data.soil_moist_pct, sensor_data.co2_ppm,
                     sensor_data.batt_mv);

            state = STATE_ZIGBEE_JOIN_CHECK;
            break;
        }

        /* ── ZIGBEE_JOIN_CHECK ─────────────────────────────────── */
        case STATE_ZIGBEE_JOIN_CHECK: {
            ESP_LOGI(TAG, "[STATE] ZIGBEE_JOIN_CHECK");

            if (!zigbee_ed_is_joined()) {
                zb_ed_config_t zb_cfg = {
                    .channel = 0,  /* Auto-scan */
                    .pan_id  = 0,
                    .install_code_enabled = false,
                };
                zigbee_ed_init(&zb_cfg);
                esp_err_t ret = zigbee_ed_start();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Zigbee join failed: %s", esp_err_to_name(ret));
                    alarm_flags |= ALARM_FLAG_ZIGBEE_TX_FAIL;
                    state = STATE_SLEEP_PREP;
                    break;
                }
            }

            /* Update Zigbee link quality */
            sensor_data.rssi_dbm = zigbee_ed_get_rssi();
            sensor_data.lqi      = zigbee_ed_get_lqi();

            state = STATE_ZIGBEE_REPORT;
            break;
        }

        /* ── ZIGBEE_REPORT ─────────────────────────────────────── */
        case STATE_ZIGBEE_REPORT: {
            ESP_LOGI(TAG, "[STATE] ZIGBEE_REPORT");

            esp_err_t ret = zigbee_ed_report_data(&sensor_data);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Zigbee report failed: %s", esp_err_to_name(ret));
                alarm_flags |= ALARM_FLAG_ZIGBEE_TX_FAIL;
            }

            state = STATE_ACTUATOR_POLL;
            break;
        }

        /* ── ACTUATOR_POLL ─────────────────────────────────────── */
        case STATE_ACTUATOR_POLL: {
            ESP_LOGI(TAG, "[STATE] ACTUATOR_POLL");

            agri_cmd_t cmd;
            if (zigbee_ed_poll_command(&cmd) == ESP_OK) {
                ESP_LOGI(TAG, "Received actuator command type=0x%02X", (int)cmd.cmd_type);
                actuator_process_command(&cmd);
            }

            state = STATE_OTA_CHECK;
            break;
        }

        /* ── OTA_CHECK ─────────────────────────────────────────── */
        case STATE_OTA_CHECK: {
            ESP_LOGI(TAG, "[STATE] OTA_CHECK");

            /* Check OTA every 24 wakeups (~12 minutes at 30s interval) */
            if ((s_wakeup_count % ZB_ED_OTA_CHECK_INTERVAL) == 0) {
                ota_node_init();
                esp_err_t ret = ota_node_query_image();
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "OTA image available — updating...");
                    /* OTA download handled by ota_node component */
                }
            }

            state = STATE_SLEEP_PREP;
            break;
        }

        /* ── SLEEP_PREP ────────────────────────────────────────── */
        case STATE_SLEEP_PREP: {
            ESP_LOGI(TAG, "[STATE] SLEEP_PREP");

            /* Wait for pending Zigbee TX to complete */
            int wait_count = 0;
            while (zigbee_ed_is_tx_pending() && wait_count < 50) {
                vTaskDelay(pdMS_TO_TICKS(100));
                wait_count++;
            }

            /* Save rain counter to NVS */
            uint32_t rain_count = rain_gauge_get_count();
            nvs_config_save_rain_count(rain_count);
            nvs_config_commit();

            /* Update alarm flags */
            sensor_data.alarm_flags = alarm_flags;

            /* Deinit peripherals */
            sensor_hub_deinit();
            actuator_deinit();

            s_wakeup_count++;

            ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

            state = STATE_DEEP_SLEEP;
            break;
        }

        /* ── DEEP_SLEEP ────────────────────────────────────────── */
        case STATE_DEEP_SLEEP: {
            ESP_LOGI(TAG, "[STATE] DEEP_SLEEP — goodnight!");
            power_manager_enter_deep_sleep();
            /* Never reaches here */
            break;
        }

        default:
            ESP_LOGE(TAG, "Unknown state %d — resetting to BOOT_INIT", (int)state);
            state = STATE_BOOT_INIT;
            break;
        }
    }
}
