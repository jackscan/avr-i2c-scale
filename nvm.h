/*
    SPDX-FileCopyrightText: 2023 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#pragma once

#include <stdint.h>

#pragma pack(push, 1)
struct calib_data {
    struct {
        uint32_t offset;
        uint16_t scale;
    } hx711;
};
#pragma pack(pop)

extern uint8_t twi_addr;
extern struct calib_data calib_data;

void nvm_init(void);
void nvm_write_calib_data(void);
void nvm_write_twi_addr(void);
