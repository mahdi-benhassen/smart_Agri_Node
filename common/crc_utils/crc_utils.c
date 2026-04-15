/*
 * SPDX-License-Identifier: Proprietary
 * Copyright (c) 2025 SAGRI Project
 *
 * crc_utils.c — CRC-16/Modbus and CRC-8 implementations
 */

#include "crc_utils.h"
#include <stdbool.h>

/* ── CRC-16/Modbus ─────────────────────────────────────────────────── */
/* Polynomial: 0x8005 (reflected: 0xA001), Init: 0xFFFF */

uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    if (data == NULL) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

bool crc16_modbus_verify(const uint8_t *frame, size_t len)
{
    if (frame == NULL || len < 3) {
        return false;
    }

    /* CRC is over data bytes (all except last 2) */
    uint16_t calculated = crc16_modbus(frame, len - 2);

    /* Modbus CRC is little-endian: low byte first */
    uint16_t received = (uint16_t)frame[len - 2] | ((uint16_t)frame[len - 1] << 8);

    return (calculated == received);
}

/* ── CRC-8/Maxim (Dallas) ──────────────────────────────────────────── */
/* Polynomial: 0x31, Init: 0xFF — used by DHT22 checksum */

uint8_t crc8_maxim(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;

    if (data == NULL) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}
