/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * crc_utils.h — CRC calculation utilities for Modbus RTU and data integrity
 */

#ifndef CRC_UTILS_H
#define CRC_UTILS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calculate CRC-16/Modbus (polynomial 0x8005, init 0xFFFF, reflect).
 *
 * Used for NPK RS-485 Modbus RTU communication.
 *
 * @param data  Pointer to data buffer
 * @param len   Number of bytes
 * @return CRC-16 value (little-endian: low byte first in Modbus frame)
 */
uint16_t crc16_modbus(const uint8_t *data, size_t len);

/**
 * @brief Calculate CRC-8 (polynomial 0x31, init 0xFF).
 *
 * Used for DHT22 sensor data validation.
 *
 * @param data  Pointer to data buffer
 * @param len   Number of bytes
 * @return CRC-8 value
 */
uint8_t crc8_maxim(const uint8_t *data, size_t len);

/**
 * @brief Verify CRC-16/Modbus on a complete Modbus frame (data + CRC).
 *
 * @param frame  Pointer to complete frame including trailing CRC bytes
 * @param len    Total frame length (data + 2 CRC bytes)
 * @return true if CRC is valid, false otherwise
 */
bool crc16_modbus_verify(const uint8_t *frame, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CRC_UTILS_H */
