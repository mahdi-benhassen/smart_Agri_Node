/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * sht40_driver.h — SHT40 high-precision temperature & humidity sensor (I2C)
 */

#ifndef SHT40_DRIVER_H
#define SHT40_DRIVER_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHT40_I2C_ADDR          0x44
#define SHT40_CMD_MEASURE_HP    0xFD    /* High precision, no heater */
#define SHT40_CMD_MEASURE_MP    0xF6    /* Medium precision */
#define SHT40_CMD_MEASURE_LP    0xE0    /* Low precision */
#define SHT40_CMD_SERIAL        0x89    /* Read serial number */
#define SHT40_CMD_SOFT_RESET    0x94    /* Soft reset */
#define SHT40_CMD_HEATER_HI_1S  0x39    /* Heater 200mW, 1s */
#define SHT40_CMD_HEATER_LO_1S  0x1E    /* Heater 20mW, 1s */
#define SHT40_MEAS_DURATION_MS  10      /* Measurement time for HP mode */

typedef struct {
    float    temperature;       /* °C, range: -40 to +125 */
    float    humidity;          /* %, range: 0 to 100 */
    bool     valid;
} sht40_data_t;

typedef struct {
    i2c_port_t i2c_port;        /* I2C port number */
    uint8_t    i2c_addr;        /* I2C address (default 0x44) */
} sht40_config_t;

esp_err_t sht40_init(const sht40_config_t *config);
esp_err_t sht40_read(sht40_data_t *data);
esp_err_t sht40_soft_reset(void);
esp_err_t sht40_read_serial(uint32_t *serial);
esp_err_t sht40_heater_pulse(void);
void      sht40_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SHT40_DRIVER_H */
