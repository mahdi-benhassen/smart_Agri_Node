/*
 * dht22_driver.c — DHT22 (AM2302) 1-Wire bit-bang driver
 *
 * FIX applied:
 *  5. The original code used taskENTER_CRITICAL() for the entire ~5ms
 *     bit-bang read, which disabled ALL interrupts on the core and starved
 *     the FreeRTOS tick, Zigbee stack timers, and Wi-Fi.
 *
 *     Replaced with esp_intr_disable / esp_intr_enable on the specific
 *     GPIO interrupt handle, which prevents spurious DHT22 GPIO edges from
 *     corrupting the read without blocking unrelated ISRs.
 *     The read is still time-critical, so we raise the task priority to
 *     configMAX_PRIORITIES-1 for its duration to minimise preemption jitter.
 */
#include "dht22_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_intr_alloc.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "DHT22";

static gpio_num_t  s_data_pin    = GPIO_NUM_NC;
static int64_t     s_last_read_us = 0;
static bool        s_initialized = false;

/* ── Internal: Microsecond delay ───────────────────────────────────── */
static inline void dht22_delay_us(uint32_t us) { ets_delay_us(us); }

/* ── Internal: Wait for pin level with timeout ─────────────────────── */
static int dht22_wait_for_level(int level, uint32_t timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(s_data_pin) != level) {
        if ((esp_timer_get_time() - start) > timeout_us) return -1;
    }
    return (int)(esp_timer_get_time() - start);
}

/* ── Internal: Read raw 40 bits (called with GPIO ISR disabled) ─────── */
static esp_err_t dht22_read_raw(uint8_t raw[5])
{
    /* Step 1: Start signal — pull low ≥ 1 ms */
    gpio_set_direction(s_data_pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(s_data_pin, 0);
    dht22_delay_us(1200);

    /* Step 2: Release — switch to input */
    gpio_set_level(s_data_pin, 1);
    gpio_set_direction(s_data_pin, GPIO_MODE_INPUT);
    dht22_delay_us(30);

    /* Step 3: Sensor pulls low ~80 µs (response) */
    if (dht22_wait_for_level(0, DHT22_TIMEOUT_US) < 0) {
        ESP_LOGW(TAG, "No response (low)"); return ESP_ERR_TIMEOUT;
    }
    if (dht22_wait_for_level(1, DHT22_TIMEOUT_US) < 0) {
        ESP_LOGW(TAG, "No response (high)"); return ESP_ERR_TIMEOUT;
    }
    if (dht22_wait_for_level(0, DHT22_TIMEOUT_US) < 0) {
        ESP_LOGW(TAG, "No data start"); return ESP_ERR_TIMEOUT;
    }

    /* Step 4: Read 40 bits */
    memset(raw, 0, 5);
    for (int i = 0; i < DHT22_DATA_BITS; i++) {
        if (dht22_wait_for_level(1, DHT22_TIMEOUT_US) < 0) {
            ESP_LOGW(TAG, "Timeout bit %d (low)", i); return ESP_ERR_TIMEOUT;
        }
        int high_us = dht22_wait_for_level(0, DHT22_TIMEOUT_US);
        if (high_us < 0) {
            ESP_LOGW(TAG, "Timeout bit %d (high)", i); return ESP_ERR_TIMEOUT;
        }
        if (high_us > 40) {
            raw[i / 8] |= (1 << (7 - (i % 8)));
        }
    }
    return ESP_OK;
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t dht22_init(const dht22_config_t *config)
{
    if (!config) { ESP_LOGE(TAG, "NULL config"); return ESP_ERR_INVALID_ARG; }

    s_data_pin = config->data_pin;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_data_pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret)); return ret; }

    s_initialized  = true;
    s_last_read_us = 0;
    ESP_LOGI(TAG, "Initialized on GPIO%d", (int)s_data_pin);
    return ESP_OK;
}

esp_err_t dht22_read(dht22_data_t *data)
{
    if (!data)          return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    data->valid = false;

    /* Enforce minimum interval */
    int64_t now_us  = esp_timer_get_time();
    int64_t elapsed = (now_us - s_last_read_us) / 1000;
    if (s_last_read_us > 0 && elapsed < DHT22_MIN_INTERVAL_MS) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)(DHT22_MIN_INTERVAL_MS - elapsed)));
    }

    /* Temporarily disable GPIO ISR service for this pin to avoid spurious
       edge callbacks corrupting timing — does NOT disable the FreeRTOS tick
       or any other peripheral interrupts.                                    */
    gpio_intr_disable(s_data_pin);

    /* Raise task priority for the ~5ms bit-bang window to reduce preemption
       jitter. configMAX_PRIORITIES-1 is the highest user-space level.       */
    UBaseType_t saved_prio = uxTaskPriorityGet(NULL);
    vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);

    uint8_t raw[5] = {0};
    esp_err_t ret  = dht22_read_raw(raw);

    /* Restore priority and re-enable GPIO ISR */
    vTaskPrioritySet(NULL, saved_prio);
    gpio_intr_enable(s_data_pin);

    s_last_read_us = esp_timer_get_time();

    if (ret != ESP_OK) { ESP_LOGW(TAG, "Read failed: %s", esp_err_to_name(ret)); return ret; }

    /* Checksum */
    uint8_t cksum = (uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]);
    if (cksum != raw[4]) {
        ESP_LOGW(TAG, "CRC mismatch: calc=0x%02X recv=0x%02X", cksum, raw[4]);
        return ESP_ERR_INVALID_CRC;
    }

    /* Parse humidity */
    uint16_t raw_hum  = ((uint16_t)raw[0] << 8) | raw[1];
    data->humidity    = (float)raw_hum * 0.1f;

    /* Parse temperature (MSB = sign) */
    uint16_t raw_temp = ((uint16_t)raw[2] << 8) | raw[3];
    if (raw_temp & 0x8000) {
        raw_temp &= 0x7FFF;
        data->temperature = -((float)raw_temp * 0.1f);
    } else {
        data->temperature = (float)raw_temp * 0.1f;
    }

    if (data->temperature < -40.0f || data->temperature > 80.0f) {
        ESP_LOGW(TAG, "Temp out of range: %.1f°C", data->temperature);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (data->humidity < 0.0f || data->humidity > 100.0f) {
        ESP_LOGW(TAG, "Hum out of range: %.1f%%", data->humidity);
        return ESP_ERR_INVALID_RESPONSE;
    }

    data->last_read_ms = (uint32_t)(s_last_read_us / 1000);
    data->valid        = true;
    ESP_LOGD(TAG, "T=%.1f°C H=%.1f%%", data->temperature, data->humidity);
    return ESP_OK;
}

void dht22_deinit(void)
{
    if (s_initialized) {
        gpio_reset_pin(s_data_pin);
        s_initialized = false;
        ESP_LOGI(TAG, "Deinitialized");
    }
}
