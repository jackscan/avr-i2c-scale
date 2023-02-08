/*
    SPDX-FileCopyrightText: 2022 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

void hx711_init(void);
void hx711_start(void);
bool hx711_is_data_available(void);
bool hx711_is_off(void);
bool hx711_is_active(void);
uint32_t hx711_data(void);
uint32_t hx711_read(void);
void hx711_powerdown(void);
void hx711_await_poweroff(void);
