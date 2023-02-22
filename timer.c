/*
    SPDX-FileCopyrightText: 2023 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#include "timer.h"

#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/io.h>

void timer_init(void) {
    TCA0.SINGLE.PER = 0xFFFF;
}

void timer_start(void) {
    TCA0.SINGLE.CTRLESET = TCA_SINGLE_CMD_RESTART_gc;
    TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1024_gc | TCA_SINGLE_ENABLE_bm;
}

void timer_stop(void) {
    TCA0.SINGLE.CTRLA &= ~TCA_SINGLE_ENABLE_bm;
}

uint16_t timer_get_time(void) {
    return TCA0.SINGLE.CNT;
}

uint16_t timer_get_time_ms(void) {
    // t * 1000 * 1024 / F_CPU
    uint32_t t = TCA0.SINGLE.CNT;
    const uint32_t f = F_CPU >> 4;
    return (uint16_t)(t * 1000UL * 64UL / f);
}
