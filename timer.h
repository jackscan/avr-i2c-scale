/*
    SPDX-FileCopyrightText: 2023 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#include <stdint.h>

void timer_init(void);
void timer_start(void);
void timer_stop(void);
uint16_t timer_get_time(void);
uint16_t timer_get_time_ms(void);
