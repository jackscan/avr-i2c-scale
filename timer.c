/*
    SPDX-FileCopyrightText: 2023 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#include "timer.h"

#include "debug.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/delay.h>

static void wait_while(uint8_t bm) {
    while ((RTC.STATUS & bm) != 0) {
    }
}

void timer_init(void) {
    // Wait for registers to synchronize
    wait_while(0xFF);

    RTC.PER = 0xffff;
    RTC.CLKSEL = RTC_CLKSEL_INT32K_gc;
}

void timer_start(void) {
    wait_while(RTC_CTRLABUSY_bm);
    RTC.CTRLA = RTC_PRESCALER_DIV32_gc;
    wait_while(RTC_CNTBUSY_bm | RTC_CTRLABUSY_bm);
    RTC.CNT = 0;
    RTC.CTRLA |= RTC_RTCEN_bm | RTC_RUNSTDBY_bm;
}

void timer_stop(void) {
    wait_while(RTC_CTRLABUSY_bm);
    RTC.CTRLA = 0;
}

uint16_t timer_get_time(void) {
    return RTC.CNT;
}

uint8_t timer_get_time_ms(void) {
    uint16_t t = RTC.CNT;
    return t * 250U / 256;
}
