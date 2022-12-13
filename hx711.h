/*
    SPDX-FileCopyrightText: 2022 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#pragma once

#include <stdint.h>

void hx711_init(void);
uint32_t hx711_read(void);
void hx711_powerdown(void);
void hx711_await_poweroff(void);
