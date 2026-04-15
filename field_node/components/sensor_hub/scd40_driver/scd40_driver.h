/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * scd40_driver.h — SCD40 CO₂, temperature & humidity sensor (I2C)
 */

#ifndef SCD40_DRIVER_H
#define SCD40_DRIVER_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCD40_I2C_ADDR              0x62
#define SCD40_CMD_START_MEAS        0x21B1
#define SCD40_CMD_STOP_MEAS         0x3F86
#define SCD40_CMD_READ_MEAS         0xEC05
#define SCD40_CMD_DATA_READY        0xE4B8
#define SCD40_CMD_SERIAL            0x3682
#define SCD40_CMD_SELF_TEST         0x3639
#define SCD40_CMD_FACTORY_RESET     0x3632
#define SCD40_CMD_REINIT            0x3646
#define SCD40_CMD_FORCED_RECAL      0x362F
#define SCD40_CMD_SET_TEMP_OFFSET   0x241D
#define SCD40_CMD_GET_TEMP_OFFSET   0x2318
#define SCD40_CMD_SET_ALTITUDE      0x2427
#define SCD40_CMD_PERSIST_SETTINGS  0x3615
#define SCD40_MEAS_INTERVAL_MS      5000    /* 5 second measurement cycle */

typedef struct {
    uint16_t co2_ppm;           /* CO₂ 400–5000 ppm */
    float    temperature;       /* °C */
    float    humidity;          /* % */
    bool     valid;
} scd40_data_t;

typedef struct {
    i2c_port_t i2c_port;
    uint8_t    i2c_addr;        /* Default: 0x62 */
} scd40_config_t;

esp_err_t scd40_init(const scd40_config_t *config);
esp_err_t scd40_read(scd40_data_t *data);
esp_err_t scd40_start_measurement(void);
esp_err_t scd40_stop_measurement(void);
esp_err_t scd40_is_data_ready(bool *ready);
esp_err_t scd40_set_temperature_offset(float offset_c);
esp_err_t scd40_set_altitude(uint16_t altitude_m);
esp_err_t scd40_force_recalibration(uint16_t target_co2_ppm);
void      scd40_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SCD40_DRIVER_H */
