/*
    SPDX-FileCopyrightText: 2023 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#include "stepper.h"

#include "config.h"
#include "debug.h"
#include "time.h"
#include "twi.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/delay.h>

#define CLKDIV     1UL
#define DIV_MS     (1000UL * CLKDIV)
#define DIV_US     (1000000UL * CLKDIV)
#define TCA_CLKSEL TCA_SINGLE_CLKSEL_DIV1_gc
#define MINP       ((F_CPU * 38UL + DIV_US - 1) / DIV_US)
#define MAXP       ((F_CPU * 19UL + DIV_MS - 1) / DIV_MS)
#define STP_HIGH_P ((F_CPU * 1UL + DIV_US - 1) / DIV_US)

struct {
    /// Time since start or time to end in units of CLKDIV/F_CPU seconds.
    uint32_t t;
    /// Number of steps taken.
    uint32_t step;
    /// Total number of steps to take.
    uint32_t total_steps;
    /// Duration of ramp up/down phase.
    uint16_t ramp;
    /// Minimum step period in timer ticks (CLKDIV/F_CPU seconds).
    uint16_t minp;
    /// Number of bits ramp is right-shifted compared to t.
    uint8_t shift;
    /// Stepper direction is either 1 or -1.
    int8_t dir;
} stepper;


ISR(TCA0_OVF_vect) {
    STP_STEP_PORT.OUTSET = STP_STEP_BIT;
    // period for next step
    uint16_t p = stepper.minp;
    uint32_t t = stepper.t >> stepper.shift;

    // adjust period during ramp up or ramp down
    if (t < stepper.ramp) {
        uint32_t x = (uint32_t)(stepper.ramp - t);
        uint32_t x2 = (uint32_t)((x * x) >> 16);
        p += (uint16_t)((x2 * x2) >> 16);
    }

    // Are total steps reached?
    if (stepper.step < stepper.total_steps) {
        TCA0.SINGLE.PERBUF = p;
        ++stepper.step;

        // increase t in first half and decrease it in second half
        int32_t mid = stepper.total_steps / 2;
        if (stepper.step + 1 < mid) {
            stepper.t += p;
        } else if (stepper.t > p) {
            stepper.t -= p;
        } else {
            stepper.t = 0;
        }
    } else {
        // Disable interrupt
        TCA0.SINGLE.INTCTRL = 0;
        // Disable timer
        TCA0.SINGLE.CTRLA = 0;
        // Disable stepper driver
        STP_NSLP_PORT.OUTCLR = STP_NSLP_BIT;
    }
    // Clear interrupt flag
    TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;

    STP_STEP_PORT.OUTCLR = STP_STEP_BIT;
}

void stepper_init(void) {
    STP_NSLP_PORT.OUTCLR = STP_NSLP_BIT;
    STP_NSLP_PORT.DIRSET = STP_NSLP_BIT;
    STP_STEP_PORT.OUTCLR = STP_STEP_BIT;
    STP_STEP_PORT.DIRSET = STP_STEP_BIT;
    STP_DIR_PORT.OUTCLR = STP_DIR_BIT;
    STP_DIR_PORT.DIRSET = STP_DIR_BIT;
}

static uint32_t stepper_period(uint32_t x) {
    uint32_t x2 = (uint32_t)((x * x) >> 16);
    return ((x2 * x2) >> 16) + stepper.minp;
}

static void stepper_calc_shift_ramp(uint32_t r) {
    uint8_t s = 0;
    for (;;) {
        if (r < 0x10000) {
            uint32_t p = stepper_period(r);
            if (p < MAXP)
                break;
        }
        r >>= 1;
        ++s;
    }

    stepper.ramp = r;
    stepper.shift = s;
}

void stepper_rotate(bool dir, uint8_t cycles, uint8_t maxspd) {

    stepper_stop();

    if (dir) {
        STP_DIR_PORT.OUTSET = STP_DIR_BIT;
    } else {
        STP_DIR_PORT.OUTCLR = STP_DIR_BIT;
    }

    // Enable stepper driver
    STP_NSLP_PORT.OUTSET = STP_NSLP_BIT;

    TCA0.SINGLE.INTCTRL = TCA_SINGLE_OVF_bm;
    TCA0.SINGLE.CNT = 0;
    TCA0.SINGLE.CMP0 = STP_HIGH_P;

    // Set initial period to 1ms for first step pulse in order to
    // allow stepper driver charge pump to stabalize.
    // Subsequent timer periods are calculated in interrupt.
    TCA0.SINGLE.PERBUF = ((F_CPU * 1UL + DIV_MS - 1) / DIV_MS);

    stepper.step = 0;
    stepper.dir = dir ? 1 : -1;
    stepper.total_steps = cycles << (3 + 4);

    // Minimum period in microseconds
    uint32_t minpt = 38UL * (255UL + 16UL) / (maxspd + 16UL);
    // const uint32_t DIV_MS = 1000UL * CLKDIV;
    stepper.minp = ((F_CPU / DIV_MS) * minpt + 1000UL - 1UL) / 1000UL;
    // uint32_t maxp = (F_CPU * 11UL + DIV_MS - 1) / DIV_MS;
    stepper.t = 0;

    // Set ramp time to half of full-speed duration.
    uint32_t rt = stepper.minp * stepper.total_steps / 2;
    stepper_calc_shift_ramp(rt);

    uint32_t r = stepper.ramp;
    uint32_t s = stepper.shift;
    // Calculate steps of ramp-up phase.
    uint32_t rs = 0;
    for (uint32_t tt = 0; tt < rt;) {
        uint32_t t = tt >> s;
        if (t < r) {
            uint32_t x = (uint32_t)(r - t);
            tt += stepper_period(x);
        } else {
            break;
        }
        ++rs;
    }

    // Adjust ramp time according to steps left in non-ramp phase.
    r = rt + stepper.minp * (stepper.total_steps / 2 - rs);
    stepper_calc_shift_ramp(r);

    LOGS("R:");
    LOGDEC_U16(stepper.ramp);
    LOGS(" S:");
    LOGDEC(stepper.shift);
    LOGS(" P:");
    LOGDEC_U16(stepper.minp);
    LOGNL();

    // Start TCA0
    TCA0.SINGLE.CTRLA = TCA_CLKSEL | TCA_SINGLE_ENABLE_bm;
}

void stepper_stop(void)
{
    // Disable interrupt
    TCA0.SINGLE.INTCTRL = 0;
    // Disable timer
    TCA0.SINGLE.CTRLA = 0;
    // Put driver to sleep
    STP_NSLP_PORT.OUTCLR = STP_NSLP_BIT;
    // Set step pin to defined level
    STP_STEP_PORT.OUTCLR = STP_STEP_BIT;
}

bool stepper_is_running(void)
{
    return TCA0.SINGLE.CTRLA != 0;
}

uint8_t stepper_get_cycle(void)
{
    return (uint8_t)(stepper.step >> 3);
}
