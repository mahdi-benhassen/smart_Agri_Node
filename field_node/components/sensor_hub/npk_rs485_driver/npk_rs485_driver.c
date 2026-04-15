/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * npk_rs485_driver.c — NPK soil nutrient sensor Modbus RTU driver
 */

#include "npk_rs485_driver.h"
#include "crc_utils.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "NPK_RS485";

static npk_config_t s_config;
static bool          s_initialized = false;

#define UART_BUF_SIZE   256
#define MODBUS_FUNC_READ_HOLDING  0x03

/* ── Internal: Set RS-485 direction ────────────────────────────────── */

static void npk_set_tx_mode(void)
{
    gpio_set_level(s_config.de_re_pin, 1);  /* DE=1, RE=1 → TX mode */
}

static void npk_set_rx_mode(void)
{
    gpio_set_level(s_config.de_re_pin, 0);  /* DE=0, RE=0 → RX mode */
}

/* ── Internal: Build Modbus Read Holding Registers request ─────────── */

static int npk_build_read_request(uint8_t *buf, uint8_t slave, uint16_t reg, uint16_t count)
{
    buf[0] = slave;
    buf[1] = MODBUS_FUNC_READ_HOLDING;
    buf[2] = (uint8_t)(reg >> 8);
    buf[3] = (uint8_t)(reg & 0xFF);
    buf[4] = (uint8_t)(count >> 8);
    buf[5] = (uint8_t)(count & 0xFF);

    uint16_t crc = crc16_modbus(buf, 6);
    buf[6] = (uint8_t)(crc & 0xFF);        /* CRC low byte first */
    buf[7] = (uint8_t)(crc >> 8);

    return 8;
}

/* ── Internal: Send request and read response ──────────────────────── */

static esp_err_t npk_transact(const uint8_t *tx_buf, int tx_len,
                               uint8_t *rx_buf, int expected_rx_len)
{
    /* Flush RX buffer */
    uart_flush_input(s_config.uart_port);

    /* Switch to TX mode and send */
    npk_set_tx_mode();
    int written = uart_write_bytes(s_config.uart_port, tx_buf, tx_len);
    if (written != tx_len) {
        ESP_LOGE(TAG, "UART write failed: %d/%d", written, tx_len);
        npk_set_rx_mode();
        return ESP_FAIL;
    }

    /* Wait for TX complete, then switch to RX */
    esp_err_t ret = uart_wait_tx_done(s_config.uart_port,
                                       pdMS_TO_TICKS(NPK_RESPONSE_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "TX wait timeout: %s", esp_err_to_name(ret));
        npk_set_rx_mode();
        return ret;
    }

    npk_set_rx_mode();

    /* Read response */
    int rx_len = uart_read_bytes(s_config.uart_port, rx_buf, expected_rx_len,
                                  pdMS_TO_TICKS(NPK_RESPONSE_TIMEOUT_MS));
    if (rx_len < expected_rx_len) {
        ESP_LOGW(TAG, "Response timeout: got %d/%d bytes", rx_len, expected_rx_len);
        return ESP_ERR_TIMEOUT;
    }

    /* Verify CRC */
    if (!crc16_modbus_verify(rx_buf, rx_len)) {
        ESP_LOGW(TAG, "Response CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    /* Verify slave address and function code */
    if (rx_buf[0] != s_config.slave_addr) {
        ESP_LOGW(TAG, "Unexpected slave addr: 0x%02X", rx_buf[0]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (rx_buf[1] & 0x80) {
        ESP_LOGW(TAG, "Modbus exception: code=0x%02X", rx_buf[2]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t npk_init(const npk_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(npk_config_t));

    if (s_config.baud_rate == 0) s_config.baud_rate = NPK_DEFAULT_BAUD;
    if (s_config.slave_addr == 0) s_config.slave_addr = NPK_DEFAULT_SLAVE_ADDR;

    /* Configure UART */
    uart_config_t uart_cfg = {
        .baud_rate  = (int)s_config.baud_rate,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(s_config.uart_port, &uart_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(s_config.uart_port, s_config.tx_pin, s_config.rx_pin,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART pin set failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_driver_install(s_config.uart_port, UART_BUF_SIZE, UART_BUF_SIZE,
                               0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure DE/RE pin for RS-485 direction control */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_config.de_re_pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DE/RE GPIO config failed: %s", esp_err_to_name(ret));
        uart_driver_delete(s_config.uart_port);
        return ret;
    }

    npk_set_rx_mode();  /* Default to receive mode */

    s_initialized = true;
    ESP_LOGI(TAG, "Initialized UART%d, baud=%lu, slave=0x%02X, DE/RE=GPIO%d",
             s_config.uart_port, (unsigned long)s_config.baud_rate,
             s_config.slave_addr, (int)s_config.de_re_pin);
    return ESP_OK;
}

esp_err_t npk_read_single(uint16_t reg_addr, uint16_t *value)
{
    if (value == NULL || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_buf[8];
    uint8_t rx_buf[16];

    int tx_len = npk_build_read_request(tx_buf, s_config.slave_addr, reg_addr, 1);

    /* Expected response: [addr][func][byte_count][data_hi][data_lo][crc_lo][crc_hi] = 7 bytes */
    esp_err_t ret = npk_transact(tx_buf, tx_len, rx_buf, 7);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Parse register value (big-endian) */
    *value = ((uint16_t)rx_buf[3] << 8) | rx_buf[4];

    if (*value > NPK_MAX_VALUE) {
        ESP_LOGW(TAG, "Value out of range: %u (max %u)", *value, NPK_MAX_VALUE);
        *value = NPK_MAX_VALUE;
    }

    return ESP_OK;
}

esp_err_t npk_read(npk_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    data->valid = false;
    esp_err_t ret;
    int errors = 0;

    /* Read Nitrogen */
    ret = npk_read_single(NPK_REG_NITROGEN, &data->nitrogen);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "N read failed: %s", esp_err_to_name(ret));
        data->nitrogen = 0;
        errors++;
    }
    vTaskDelay(pdMS_TO_TICKS(50));  /* Inter-frame delay */

    /* Read Phosphorus */
    ret = npk_read_single(NPK_REG_PHOSPHORUS, &data->phosphorus);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "P read failed: %s", esp_err_to_name(ret));
        data->phosphorus = 0;
        errors++;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Read Potassium */
    ret = npk_read_single(NPK_REG_POTASSIUM, &data->potassium);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "K read failed: %s", esp_err_to_name(ret));
        data->potassium = 0;
        errors++;
    }

    if (errors >= 3) {
        ESP_LOGE(TAG, "All NPK reads failed");
        return ESP_FAIL;
    }

    data->valid = (errors == 0);
    ESP_LOGD(TAG, "N=%u P=%u K=%u mg/L (errors=%d)",
             data->nitrogen, data->phosphorus, data->potassium, errors);
    return ESP_OK;
}

void npk_deinit(void)
{
    if (s_initialized) {
        uart_driver_delete(s_config.uart_port);
        gpio_reset_pin(s_config.de_re_pin);
        s_initialized = false;
        ESP_LOGI(TAG, "Deinitialized");
    }
}
