/*
    SPDX-FileCopyrightText: 2023 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TWI_BUFFER_SIZE 8

/// Reset value of temperature.
#define TWI_TEMP_INVALID (-128)
/// Confirmation byte for TWI_CMD_CALIB_WRITE.
#define TWI_CONFIRM_CALIB_WRITE 0x3A
/// Confirmation byte for TWI_CMD_ADDR_WRITE.
#define TWI_CONFIRM_ADDR_WRITE 0x6A
/// Confirmation byte for TWI_CMD_DISABLE_WD.
#define TWI_CONFIRM_DISABLE_WD 0x9A

/// TWI commands.
enum {
    TWI_CMD_SLEEP = 0x00,
    TWI_CMD_MEASURE_WEIGHT = 0x50,
    TWI_CMD_TRACK_WEIGHT = 0x51,
    TWI_CMD_OPEN_VALVE = 0x52,
    TWI_CMD_CLOSE_VALVE = 0x53,
    TWI_CMD_GET_TEMP = 0x54,
    TWI_CMD_GET_CALIB = 0x55,
    TWI_CMD_SET_CALIB = 0x56,
    TWI_CMD_ENABLE_WD = 0x57,
    TWI_CMD_CALIB_WRITE = 0xA0,
    TWI_CMD_SET_ADDR = 0xA3,
    TWI_CMD_ADDR_WRITE = 0xA6,
    TWI_CMD_DISABLE_WD = 0xA9,
    TWI_CMD_GET_VERSION = 0xE0,
    TWI_CMD_NONE = 0xFF,
};

struct twi_data {
    uint8_t task;
    uint8_t count;
    uint8_t buf[TWI_BUFFER_SIZE];
};

void twi_init(uint8_t addr);
void twi_reset(void);
bool twi_task_pending(void);
void twi_write(uint8_t count, const uint8_t *data);
void twi_write_P(uint8_t count, const __flash uint8_t *data);
void twi_read(struct twi_data *data);
uint8_t twi_get_send_count(void);

#ifndef NDEBUG
void twi_dump_dbg(void);
#else
static inline void twi_dump_dbg(void) {}
#endif
