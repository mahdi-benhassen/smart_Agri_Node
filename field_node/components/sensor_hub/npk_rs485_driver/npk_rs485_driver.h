/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * npk_rs485_driver.h — NPK soil nutrient sensor via Modbus RTU / RS-485
 */

#ifndef NPK_RS485_DRIVER_H
#define NPK_RS485_DRIVER_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NPK_DEFAULT_BAUD        9600
#define NPK_DEFAULT_SLAVE_ADDR  0x01
#define NPK_RESPONSE_TIMEOUT_MS 1000
#define NPK_MAX_VALUE           1999    /* 0–1999 mg/L */

/* Modbus register addresses for N/P/K */
#define NPK_REG_NITROGEN        0x001E
#define NPK_REG_PHOSPHORUS      0x001F
#define NPK_REG_POTASSIUM       0x0020

typedef struct {
    uint16_t nitrogen;          /* N mg/L (0–1999) */
    uint16_t phosphorus;        /* P mg/L (0–1999) */
    uint16_t potassium;         /* K mg/L (0–1999) */
    bool     valid;
} npk_data_t;

typedef struct {
    uart_port_t  uart_port;
    gpio_num_t   tx_pin;
    gpio_num_t   rx_pin;
    gpio_num_t   de_re_pin;         /* RS-485 DE/RE control (MAX3485) */
    uint32_t     baud_rate;         /* Default: 9600 */
    uint8_t      slave_addr;        /* Modbus slave address (default: 0x01) */
} npk_config_t;

esp_err_t npk_init(const npk_config_t *config);
esp_err_t npk_read(npk_data_t *data);
esp_err_t npk_read_single(uint16_t reg_addr, uint16_t *value);
void      npk_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* NPK_RS485_DRIVER_H */
