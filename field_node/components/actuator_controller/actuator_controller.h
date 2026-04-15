/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * actuator_controller.h — Relay, PWM, and PID actuator control
 */

#ifndef ACTUATOR_CONTROLLER_H
#define ACTUATOR_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "agri_data_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── PID Controller ────────────────────────────────────────────────── */
typedef struct {
    float kp;
    float ki;
    float kd;
    float setpoint;
    float integral;
    float prev_error;
    float output_min;
    float output_max;
    float dt;               /* Time step in seconds */
} pid_controller_t;

/* ── GPIO Pin Configuration ────────────────────────────────────────── */
typedef struct {
    /* Relay outputs */
    gpio_num_t valve_pin;           /* Irrigation valve relay */
    gpio_num_t valve_feedback_pin;  /* Current sensor ADC (-1 if none) */

    /* PWM outputs (LEDC channels) */
    gpio_num_t pump_pin;            /* Water pump PWM */
    gpio_num_t fan_pin;             /* Ventilation fan PWM */
    gpio_num_t led_r_pin;           /* LED grow light red */
    gpio_num_t led_b_pin;           /* LED grow light blue */
    gpio_num_t led_g_pin;           /* LED grow light green (-1 if none) */
    gpio_num_t led_w_pin;           /* LED grow light white (-1 if none) */

    /* Fertigation */
    gpio_num_t fert_pump_pin;       /* Fertigation dosing pump */
    gpio_num_t fert_flow_pin;       /* Flow meter pulse input */

    /* Fan PID parameters */
    float fan_pid_kp;
    float fan_pid_ki;
    float fan_pid_kd;
    float fan_temp_setpoint;        /* Target temperature °C */
} actuator_config_t;

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t actuator_init(const actuator_config_t *config);
esp_err_t actuator_set_valve(bool open);
esp_err_t actuator_set_pump(uint8_t duty_pct);
esp_err_t actuator_set_fan(uint8_t duty_pct);
esp_err_t actuator_set_led_r(uint8_t duty_pct);
esp_err_t actuator_set_led_b(uint8_t duty_pct);
esp_err_t actuator_process_command(const agri_cmd_t *cmd);
esp_err_t actuator_fan_pid_update(float current_temp);
bool      actuator_get_valve_state(void);
uint8_t   actuator_get_pump_pct(void);
uint8_t   actuator_get_fan_pct(void);
uint8_t   actuator_get_led_r_pct(void);
uint8_t   actuator_get_led_b_pct(void);
void      actuator_deinit(void);

/* PID utility */
void pid_init(pid_controller_t *pid, float kp, float ki, float kd,
              float setpoint, float out_min, float out_max, float dt);
float pid_compute(pid_controller_t *pid, float measurement);
void  pid_reset(pid_controller_t *pid);

#ifdef __cplusplus
}
#endif

#endif /* ACTUATOR_CONTROLLER_H */
