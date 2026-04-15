/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * actuator_controller.c — Relay, PWM, PID actuator control implementation
 */

#include "actuator_controller.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "ACTUATOR";

static actuator_config_t s_config;
static pid_controller_t  s_fan_pid;
static bool              s_initialized = false;

/* Actuator state */
static bool    s_valve_open   = false;
static uint8_t s_pump_pct     = 0;
static uint8_t s_fan_pct      = 0;
static uint8_t s_led_r_pct    = 0;
static uint8_t s_led_b_pct    = 0;

/* LEDC channel assignments */
#define LEDC_PUMP_CHANNEL    LEDC_CHANNEL_0
#define LEDC_FAN_CHANNEL     LEDC_CHANNEL_1
#define LEDC_LED_R_CHANNEL   LEDC_CHANNEL_2
#define LEDC_LED_B_CHANNEL   LEDC_CHANNEL_3
#define LEDC_TIMER           LEDC_TIMER_0
#define LEDC_FREQ_HZ         5000
#define LEDC_RESOLUTION      LEDC_TIMER_10_BIT
#define LEDC_MAX_DUTY        1023

/* ── PID Controller ────────────────────────────────────────────────── */

void pid_init(pid_controller_t *pid, float kp, float ki, float kd,
              float setpoint, float out_min, float out_max, float dt)
{
    if (pid == NULL) return;
    pid->kp         = kp;
    pid->ki         = ki;
    pid->kd         = kd;
    pid->setpoint   = setpoint;
    pid->integral   = 0.0f;
    pid->prev_error = 0.0f;
    pid->output_min = out_min;
    pid->output_max = out_max;
    pid->dt         = dt;
}

float pid_compute(pid_controller_t *pid, float measurement)
{
    if (pid == NULL) return 0.0f;

    float error = pid->setpoint - measurement;

    /* Proportional */
    float p_term = pid->kp * error;

    /* Integral with anti-windup */
    pid->integral += error * pid->dt;
    float i_max = (pid->output_max - pid->output_min) / (pid->ki + 0.001f);
    if (pid->integral > i_max) pid->integral = i_max;
    if (pid->integral < -i_max) pid->integral = -i_max;
    float i_term = pid->ki * pid->integral;

    /* Derivative */
    float derivative = (error - pid->prev_error) / pid->dt;
    float d_term = pid->kd * derivative;
    pid->prev_error = error;

    /* Output with clamping */
    float output = p_term + i_term + d_term;
    if (output > pid->output_max) output = pid->output_max;
    if (output < pid->output_min) output = pid->output_min;

    return output;
}

void pid_reset(pid_controller_t *pid)
{
    if (pid == NULL) return;
    pid->integral  = 0.0f;
    pid->prev_error = 0.0f;
}

/* ── Internal: LEDC setup per channel ──────────────────────────────── */

static esp_err_t ledc_channel_setup(gpio_num_t pin, ledc_channel_t channel)
{
    if ((int)pin < 0) return ESP_OK;  /* Pin not configured */

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = channel,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };

    return ledc_channel_config(&ch_cfg);
}

static esp_err_t ledc_set_duty_pct(ledc_channel_t channel, uint8_t pct)
{
    uint32_t duty = ((uint32_t)pct * LEDC_MAX_DUTY) / 100;
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    if (ret != ESP_OK) return ret;
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t actuator_init(const actuator_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(actuator_config_t));

    /* Configure valve relay GPIO */
    if ((int)s_config.valve_pin >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << s_config.valve_pin),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Valve GPIO config failed: %s", esp_err_to_name(ret));
            return ret;
        }
        gpio_set_level(s_config.valve_pin, 0);  /* Start closed */
    }

    /* Configure LEDC timer */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num       = LEDC_TIMER,
        .freq_hz         = LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };

    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure PWM channels */
    ret = ledc_channel_setup(s_config.pump_pin, LEDC_PUMP_CHANNEL);
    if (ret != ESP_OK) ESP_LOGW(TAG, "Pump PWM setup failed");

    ret = ledc_channel_setup(s_config.fan_pin, LEDC_FAN_CHANNEL);
    if (ret != ESP_OK) ESP_LOGW(TAG, "Fan PWM setup failed");

    ret = ledc_channel_setup(s_config.led_r_pin, LEDC_LED_R_CHANNEL);
    if (ret != ESP_OK) ESP_LOGW(TAG, "LED R PWM setup failed");

    ret = ledc_channel_setup(s_config.led_b_pin, LEDC_LED_B_CHANNEL);
    if (ret != ESP_OK) ESP_LOGW(TAG, "LED B PWM setup failed");

    /* Initialize fan PID */
    float kp = s_config.fan_pid_kp > 0 ? s_config.fan_pid_kp : 5.0f;
    float ki = s_config.fan_pid_ki > 0 ? s_config.fan_pid_ki : 0.5f;
    float kd = s_config.fan_pid_kd > 0 ? s_config.fan_pid_kd : 1.0f;
    float sp = s_config.fan_temp_setpoint > 0 ? s_config.fan_temp_setpoint : 25.0f;
    pid_init(&s_fan_pid, kp, ki, kd, sp, 0.0f, 100.0f, 30.0f);

    s_initialized = true;
    ESP_LOGI(TAG, "Initialized — Valve:GPIO%d Pump:GPIO%d Fan:GPIO%d LED_R:GPIO%d LED_B:GPIO%d",
             (int)s_config.valve_pin, (int)s_config.pump_pin, (int)s_config.fan_pin,
             (int)s_config.led_r_pin, (int)s_config.led_b_pin);
    return ESP_OK;
}

esp_err_t actuator_set_valve(bool open)
{
    if (!s_initialized || (int)s_config.valve_pin < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = gpio_set_level(s_config.valve_pin, open ? 1 : 0);
    if (ret == ESP_OK) {
        s_valve_open = open;
        ESP_LOGI(TAG, "Valve %s", open ? "OPEN" : "CLOSED");
    }
    return ret;
}

esp_err_t actuator_set_pump(uint8_t duty_pct)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (duty_pct > 100) duty_pct = 100;

    esp_err_t ret = ledc_set_duty_pct(LEDC_PUMP_CHANNEL, duty_pct);
    if (ret == ESP_OK) {
        s_pump_pct = duty_pct;
        ESP_LOGI(TAG, "Pump duty=%u%%", duty_pct);
    }
    return ret;
}

esp_err_t actuator_set_fan(uint8_t duty_pct)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (duty_pct > 100) duty_pct = 100;

    esp_err_t ret = ledc_set_duty_pct(LEDC_FAN_CHANNEL, duty_pct);
    if (ret == ESP_OK) {
        s_fan_pct = duty_pct;
        ESP_LOGD(TAG, "Fan duty=%u%%", duty_pct);
    }
    return ret;
}

esp_err_t actuator_set_led_r(uint8_t duty_pct)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (duty_pct > 100) duty_pct = 100;

    esp_err_t ret = ledc_set_duty_pct(LEDC_LED_R_CHANNEL, duty_pct);
    if (ret == ESP_OK) {
        s_led_r_pct = duty_pct;
        ESP_LOGD(TAG, "LED_R duty=%u%%", duty_pct);
    }
    return ret;
}

esp_err_t actuator_set_led_b(uint8_t duty_pct)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (duty_pct > 100) duty_pct = 100;

    esp_err_t ret = ledc_set_duty_pct(LEDC_LED_B_CHANNEL, duty_pct);
    if (ret == ESP_OK) {
        s_led_b_pct = duty_pct;
        ESP_LOGD(TAG, "LED_B duty=%u%%", duty_pct);
    }
    return ret;
}

esp_err_t actuator_fan_pid_update(float current_temp)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    float output = pid_compute(&s_fan_pid, current_temp);
    uint8_t duty = (uint8_t)output;
    return actuator_set_fan(duty);
}

esp_err_t actuator_process_command(const agri_cmd_t *cmd)
{
    if (cmd == NULL || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Processing cmd type=0x%02X id=%lu",
             (int)cmd->cmd_type, (unsigned long)cmd->cmd_id);

    switch (cmd->cmd_type) {
    case AGRI_CMD_VALVE_SET:
        return actuator_set_valve(cmd->payload.valve_state);

    case AGRI_CMD_PUMP_SET:
        return actuator_set_pump(cmd->payload.duty_pct);

    case AGRI_CMD_FAN_SET:
        return actuator_set_fan(cmd->payload.duty_pct);

    case AGRI_CMD_LED_R_SET:
        return actuator_set_led_r(cmd->payload.led.led_r_pct);

    case AGRI_CMD_LED_B_SET:
        return actuator_set_led_b(cmd->payload.led.led_b_pct);

    case AGRI_CMD_RAIN_RESET:
        ESP_LOGI(TAG, "Rain counter reset requested (handled by sensor hub)");
        return ESP_OK;

    default:
        ESP_LOGW(TAG, "Unknown command type: 0x%02X", (int)cmd->cmd_type);
        return ESP_ERR_NOT_SUPPORTED;
    }
}

bool    actuator_get_valve_state(void) { return s_valve_open; }
uint8_t actuator_get_pump_pct(void)    { return s_pump_pct; }
uint8_t actuator_get_fan_pct(void)     { return s_fan_pct; }
uint8_t actuator_get_led_r_pct(void)   { return s_led_r_pct; }
uint8_t actuator_get_led_b_pct(void)   { return s_led_b_pct; }

void actuator_deinit(void)
{
    if (s_initialized) {
        actuator_set_valve(false);
        actuator_set_pump(0);
        actuator_set_fan(0);
        actuator_set_led_r(0);
        actuator_set_led_b(0);
        s_initialized = false;
        ESP_LOGI(TAG, "Deinitialized — all actuators OFF");
    }
}
