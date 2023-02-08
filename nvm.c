/*
    SPDX-FileCopyrightText: 2021 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#include "nvm.h"

#include <avr/eeprom.h>

struct calib_data calib_data = {};
uint8_t twi_addr;
static EEMEM struct calib_data nvm_calib_data;
static EEMEM uint8_t nvm_twi_addr;

void nvm_init(void) {
    twi_addr = eeprom_read_byte(&nvm_twi_addr);
    eeprom_read_block(&calib_data, &nvm_calib_data, sizeof calib_data);

    if (twi_addr == 0xFF)
        twi_addr = 0x40;

    if (calib_data.hx711.offset == 0xFFFFFFFF &&
        calib_data.hx711.scale == 0xFFFF) {
        calib_data.hx711.scale = 256;
        calib_data.hx711.offset = 0;
    }
}

void nvm_write_calib_data(void) {
    eeprom_update_block(&calib_data, &nvm_calib_data, sizeof calib_data);
}

void nvm_write_twi_addr(void) {
    eeprom_update_byte(&nvm_twi_addr, twi_addr);
}
