/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * bh1750_driver.h — BH1750 ambient light sensor (I2C)
 */

#ifndef BH1750_DRIVER_H
#define BH1750_DRIVER_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BH1750_I2C_ADDR_LOW     0x23    /* ADDR pin LOW */
#define BH1750_I2C_ADDR_HIGH    0x5C    /* ADDR pin HIGH */

#define BH1750_CMD_POWER_ON     0x01
#define BH1750_CMD_POWER_OFF    0x00
#define BH1750_CMD_RESET        0x07
#define BH1750_CMD_CONT_H_RES   0x10    /* Continuous high-res mode (1 lx) */
#define BH1750_CMD_CONT_H_RES2  0x11    /* Continuous high-res mode 2 (0.5 lx) */
#define BH1750_CMD_CONT_L_RES   0x13    /* Continuous low-res mode (4 lx) */
#define BH1750_CMD_ONE_H_RES    0x20    /* One-time high-res */
#define BH1750_CMD_ONE_H_RES2   0x21    /* One-time high-res 2 */
#define BH1750_CMD_ONE_L_RES    0x23    /* One-time low-res */

#define BH1750_MEAS_TIME_MS     180     /* Max measurement time (high-res) */

typedef struct {
    uint32_t lux;                   /* Light intensity (1–65535 lux) */
    bool     valid;
} bh1750_data_t;

typedef struct {
    i2c_port_t i2c_port;
    uint8_t    i2c_addr;            /* Default: 0x23 */
} bh1750_config_t;

esp_err_t bh1750_init(const bh1750_config_t *config);
esp_err_t bh1750_read(bh1750_data_t *data);
esp_err_t bh1750_set_mode(uint8_t mode_cmd);
esp_err_t bh1750_power_down(void);
void      bh1750_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BH1750_DRIVER_H */
