/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * rain_gauge_driver.c — Tipping-bucket rain gauge ISR driver with debounce
 */

#include "rain_gauge_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "RAIN_GAUGE";

static volatile uint32_t s_pulse_count = 0;
static volatile int64_t  s_last_isr_us = 0;
static gpio_num_t        s_pulse_pin = GPIO_NUM_NC;
static bool              s_initialized = false;

/* ── ISR Handler ───────────────────────────────────────────────────── */

static void IRAM_ATTR rain_gauge_isr_handler(void *arg)
{
    int64_t now = esp_timer_get_time();
    int64_t elapsed_ms = (now - s_last_isr_us) / 1000;

    /* Debounce: ignore if within debounce window */
    if (elapsed_ms >= RAIN_GAUGE_DEBOUNCE_MS) {
        s_pulse_count++;
        s_last_isr_us = now;
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t rain_gauge_init(const rain_gauge_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    s_pulse_pin = config->pulse_pin;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_pulse_pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = config->edge ? config->edge : GPIO_INTR_NEGEDGE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Install ISR service if not already installed */
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "ISR service install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_isr_handler_add(s_pulse_pin, rain_gauge_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ISR handler add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_pulse_count = 0;
    s_last_isr_us = 0;
    s_initialized = true;

    ESP_LOGI(TAG, "Initialized on GPIO%d (%.1f mm/pulse, %d ms debounce)",
             (int)s_pulse_pin, RAIN_GAUGE_MM_PER_PULSE, RAIN_GAUGE_DEBOUNCE_MS);
    return ESP_OK;
}

esp_err_t rain_gauge_read(rain_gauge_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    data->pulse_count = s_pulse_count;
    data->rain_mm = (float)s_pulse_count * RAIN_GAUGE_MM_PER_PULSE;
    data->valid = true;

    ESP_LOGD(TAG, "Pulses=%lu rain=%.1f mm",
             (unsigned long)data->pulse_count, data->rain_mm);
    return ESP_OK;
}

esp_err_t rain_gauge_reset(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&mux);
    s_pulse_count = 0;
    taskEXIT_CRITICAL(&mux);

    ESP_LOGI(TAG, "Counter reset");
    return ESP_OK;
}

uint32_t rain_gauge_get_count(void)
{
    return s_pulse_count;
}

void rain_gauge_set_count(uint32_t count)
{
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&mux);
    s_pulse_count = count;
    taskEXIT_CRITICAL(&mux);
}

void rain_gauge_deinit(void)
{
    if (s_initialized) {
        gpio_isr_handler_remove(s_pulse_pin);
        gpio_reset_pin(s_pulse_pin);
        s_initialized = false;
        ESP_LOGI(TAG, "Deinitialized");
    }
}
