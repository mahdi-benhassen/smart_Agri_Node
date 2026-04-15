/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * sensor_hub.c — Sensor hub: orchestrates all sensor drivers
 */

#include "sensor_hub.h"
#include "dht22_driver.h"
#include "sht40_driver.h"
#include "soil_moisture_driver.h"
#include "npk_rs485_driver.h"
#include "rain_gauge_driver.h"
#include "bh1750_driver.h"
#include "scd40_driver.h"
#include "agri_data_model.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "SENSOR_HUB";

static agri_calibration_t s_calibration;
static bool s_initialized = false;

/* Event bits for parallel sensor acquisition */
#define SENSOR_BIT_DHT22    (1 << 0)
#define SENSOR_BIT_SHT40    (1 << 1)
#define SENSOR_BIT_SOIL     (1 << 2)
#define SENSOR_BIT_NPK      (1 << 3)
#define SENSOR_BIT_RAIN     (1 << 4)
#define SENSOR_BIT_BH1750   (1 << 5)
#define SENSOR_BIT_SCD40    (1 << 6)
#define SENSOR_BITS_ALL     (0x7F)

/* Shared context for parallel tasks */
typedef struct {
    agri_sensor_data_t *data;
    uint16_t           *alarm_flags;
    EventGroupHandle_t  event_group;
} sensor_ctx_t;

/* ── I2C Bus Init ──────────────────────────────────────────────────── */

static esp_err_t sensor_hub_i2c_init(const sensor_hub_config_t *cfg)
{
    i2c_config_t i2c_cfg = {
        .mode       = I2C_MODE_MASTER,
        .sda_io_num = cfg->i2c_sda,
        .scl_io_num = cfg->i2c_scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = cfg->i2c_freq_hz ? cfg->i2c_freq_hz : 100000,
    };

    esp_err_t ret = i2c_param_config(cfg->i2c_port, &i2c_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(cfg->i2c_port, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C%d initialized: SDA=%d SCL=%d freq=%lu",
             cfg->i2c_port, cfg->i2c_sda, cfg->i2c_scl,
             (unsigned long)(cfg->i2c_freq_hz ? cfg->i2c_freq_hz : 100000));
    return ESP_OK;
}

/* ── Parallel Sensor Read Tasks ────────────────────────────────────── */

static void task_read_dht22(void *arg)
{
    sensor_ctx_t *ctx = (sensor_ctx_t *)arg;
    dht22_data_t d;

    if (dht22_read(&d) == ESP_OK && d.valid) {
        ctx->data->temp_c = d.temperature;
        ctx->data->humidity_pct = d.humidity;
    } else {
        *ctx->alarm_flags |= ALARM_FLAG_DHT22_FAULT;
        ESP_LOGW(TAG, "DHT22 read failed");
    }

    xEventGroupSetBits(ctx->event_group, SENSOR_BIT_DHT22);
    vTaskDelete(NULL);
}

static void task_read_sht40(void *arg)
{
    sensor_ctx_t *ctx = (sensor_ctx_t *)arg;
    sht40_data_t d;

    if (sht40_read(&d) == ESP_OK && d.valid) {
        /* SHT40 is higher precision — override DHT22 if valid */
        ctx->data->temp_c = d.temperature;
        ctx->data->humidity_pct = d.humidity;
    } else {
        *ctx->alarm_flags |= ALARM_FLAG_SHT40_FAULT;
        ESP_LOGW(TAG, "SHT40 read failed");
    }

    xEventGroupSetBits(ctx->event_group, SENSOR_BIT_SHT40);
    vTaskDelete(NULL);
}

static void task_read_soil(void *arg)
{
    sensor_ctx_t *ctx = (sensor_ctx_t *)arg;
    soil_moisture_data_t d;

    if (soil_moisture_read(&d) == ESP_OK && d.valid) {
        ctx->data->soil_moist_pct = d.vwc_pct;
        ctx->data->soil_temp_c = d.soil_temp_c;
    } else {
        *ctx->alarm_flags |= ALARM_FLAG_SOIL_FAULT;
        ESP_LOGW(TAG, "Soil moisture read failed");
    }

    xEventGroupSetBits(ctx->event_group, SENSOR_BIT_SOIL);
    vTaskDelete(NULL);
}

static void task_read_npk(void *arg)
{
    sensor_ctx_t *ctx = (sensor_ctx_t *)arg;
    npk_data_t d;

    if (npk_read(&d) == ESP_OK && d.valid) {
        ctx->data->npk_n = d.nitrogen;
        ctx->data->npk_p = d.phosphorus;
        ctx->data->npk_k = d.potassium;
    } else {
        *ctx->alarm_flags |= ALARM_FLAG_NPK_FAULT;
        ESP_LOGW(TAG, "NPK read failed");
    }

    xEventGroupSetBits(ctx->event_group, SENSOR_BIT_NPK);
    vTaskDelete(NULL);
}

static void task_read_rain(void *arg)
{
    sensor_ctx_t *ctx = (sensor_ctx_t *)arg;
    rain_gauge_data_t d;

    if (rain_gauge_read(&d) == ESP_OK && d.valid) {
        ctx->data->rain_mm = d.rain_mm;
    } else {
        *ctx->alarm_flags |= ALARM_FLAG_RAIN_FAULT;
    }

    xEventGroupSetBits(ctx->event_group, SENSOR_BIT_RAIN);
    vTaskDelete(NULL);
}

static void task_read_bh1750(void *arg)
{
    sensor_ctx_t *ctx = (sensor_ctx_t *)arg;
    bh1750_data_t d;

    if (bh1750_read(&d) == ESP_OK && d.valid) {
        ctx->data->lux = d.lux;
    } else {
        *ctx->alarm_flags |= ALARM_FLAG_BH1750_FAULT;
        ESP_LOGW(TAG, "BH1750 read failed");
    }

    xEventGroupSetBits(ctx->event_group, SENSOR_BIT_BH1750);
    vTaskDelete(NULL);
}

static void task_read_scd40(void *arg)
{
    sensor_ctx_t *ctx = (sensor_ctx_t *)arg;
    scd40_data_t d;

    if (scd40_read(&d) == ESP_OK && d.valid) {
        ctx->data->co2_ppm = d.co2_ppm;
    } else {
        *ctx->alarm_flags |= ALARM_FLAG_SCD40_FAULT;
        ESP_LOGW(TAG, "SCD40 read failed");
    }

    xEventGroupSetBits(ctx->event_group, SENSOR_BIT_SCD40);
    vTaskDelete(NULL);
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t sensor_hub_init(const sensor_hub_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_calibration, &config->calibration, sizeof(agri_calibration_t));
    int errors = 0;

    /* Initialize I2C bus */
    esp_err_t ret = sensor_hub_i2c_init(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed — I2C sensors unavailable");
        errors++;
    }

    /* Initialize DHT22 */
    dht22_config_t dht_cfg = { .data_pin = config->dht22_pin };
    if (dht22_init(&dht_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "DHT22 init failed");
        errors++;
    }

    /* Initialize SHT40 */
    sht40_config_t sht_cfg = {
        .i2c_port = config->i2c_port,
        .i2c_addr = SHT40_I2C_ADDR,
    };
    if (sht40_init(&sht_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "SHT40 init failed");
        errors++;
    }

    /* Initialize soil moisture */
    soil_moisture_config_t soil_cfg = {
        .adc_unit     = config->soil_adc_unit,
        .channel      = config->soil_adc_channel,
        .temp_channel = config->soil_temp_channel,
        .attenuation  = ADC_ATTEN_DB_12,
        .cal_dry      = config->soil_cal_dry,
        .cal_wet      = config->soil_cal_wet,
    };
    if (soil_moisture_init(&soil_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "Soil moisture init failed");
        errors++;
    }

    /* Initialize NPK RS-485 */
    npk_config_t npk_cfg = {
        .uart_port  = config->npk_uart_port,
        .tx_pin     = config->npk_tx_pin,
        .rx_pin     = config->npk_rx_pin,
        .de_re_pin  = config->npk_de_re_pin,
        .baud_rate  = NPK_DEFAULT_BAUD,
        .slave_addr = NPK_DEFAULT_SLAVE_ADDR,
    };
    if (npk_init(&npk_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "NPK init failed");
        errors++;
    }

    /* Initialize rain gauge */
    rain_gauge_config_t rain_cfg = {
        .pulse_pin = config->rain_pin,
        .edge      = GPIO_INTR_NEGEDGE,
    };
    if (rain_gauge_init(&rain_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "Rain gauge init failed");
        errors++;
    }

    /* Initialize BH1750 */
    bh1750_config_t bh_cfg = {
        .i2c_port = config->i2c_port,
        .i2c_addr = BH1750_I2C_ADDR_LOW,
    };
    if (bh1750_init(&bh_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "BH1750 init failed");
        errors++;
    }

    /* Initialize SCD40 */
    scd40_config_t scd_cfg = {
        .i2c_port = config->i2c_port,
        .i2c_addr = SCD40_I2C_ADDR,
    };
    if (scd40_init(&scd_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "SCD40 init failed");
        errors++;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Initialized (%d/%d sensors OK)", 7 - errors, 7);
    return ESP_OK;
}

esp_err_t sensor_hub_acquire_all(agri_sensor_data_t *data, uint16_t *alarm_flags)
{
    if (data == NULL || alarm_flags == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    EventGroupHandle_t eg = xEventGroupCreate();
    if (eg == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    sensor_ctx_t ctx = {
        .data        = data,
        .alarm_flags = alarm_flags,
        .event_group = eg,
    };

    /* Launch parallel sensor tasks */
    BaseType_t ok;
    ok = xTaskCreate(task_read_dht22,  "dht22",  4096, &ctx, 4, NULL);
    if (ok != pdPASS) ESP_LOGW(TAG, "DHT22 task create failed");

    ok = xTaskCreate(task_read_sht40,  "sht40",  4096, &ctx, 4, NULL);
    if (ok != pdPASS) ESP_LOGW(TAG, "SHT40 task create failed");

    ok = xTaskCreate(task_read_soil,   "soil",   4096, &ctx, 4, NULL);
    if (ok != pdPASS) ESP_LOGW(TAG, "Soil task create failed");

    ok = xTaskCreate(task_read_npk,    "npk",    4096, &ctx, 4, NULL);
    if (ok != pdPASS) ESP_LOGW(TAG, "NPK task create failed");

    ok = xTaskCreate(task_read_rain,   "rain",   4096, &ctx, 4, NULL);
    if (ok != pdPASS) ESP_LOGW(TAG, "Rain task create failed");

    ok = xTaskCreate(task_read_bh1750, "bh1750", 4096, &ctx, 4, NULL);
    if (ok != pdPASS) ESP_LOGW(TAG, "BH1750 task create failed");

    ok = xTaskCreate(task_read_scd40,  "scd40",  4096, &ctx, 4, NULL);
    if (ok != pdPASS) ESP_LOGW(TAG, "SCD40 task create failed");

    /* Wait for all sensors (10 second max timeout) */
    EventBits_t bits = xEventGroupWaitBits(eg, SENSOR_BITS_ALL,
                                            pdTRUE, pdTRUE,
                                            pdMS_TO_TICKS(10000));

    if ((bits & SENSOR_BITS_ALL) != SENSOR_BITS_ALL) {
        ESP_LOGW(TAG, "Sensor acquisition timeout (bits=0x%lX)", (unsigned long)bits);
    }

    vEventGroupDelete(eg);

    /* Apply calibration */
    agri_data_apply_calibration(data, &s_calibration);

    ESP_LOGI(TAG, "Acquisition complete: T=%.1f H=%.1f SM=%.1f CO2=%u Lux=%lu",
             data->temp_c, data->humidity_pct, data->soil_moist_pct,
             data->co2_ppm, (unsigned long)data->lux);

    return ESP_OK;
}

void sensor_hub_deinit(void)
{
    dht22_deinit();
    sht40_deinit();
    soil_moisture_deinit();
    npk_deinit();
    rain_gauge_deinit();
    bh1750_deinit();
    scd40_deinit();

    s_initialized = false;
    ESP_LOGI(TAG, "All sensors deinitialized");
}
