/*
    SPDX-FileCopyrightText: 2022 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#include "config.h"
#include "debug.h"
#include "hx711.h"
#include "version.h"
#include "buckets.h"

#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <util/delay.h>

#if GIT_DIRTY
#define GIT_DIRTY_SUFFIX "-dirty"
#else
#define GIT_DIRTY_SUFFIX ""
#endif

#define STR(S)       #S
#define STRINGIFY(S) STR(S)

#define VERSION_STR                                                            \
    STRINGIFY(VERSION_MAJOR)                                                   \
    "." STRINGIFY(VERSION_MINOR) "." STRINGIFY(VERSION_PATCH) " (" STRINGIFY(  \
        GIT_HASH) GIT_DIRTY_SUFFIX ")"

static void early_init(void) {
    cli();
    // disable digital input on all pins
    register8_t *porta_pintctrl = &PORTA.PIN0CTRL;
    register8_t *portb_pintctrl = &PORTB.PIN0CTRL;
    for (uint8_t i = 0; i < 8; ++i) {
        porta_pintctrl[i] = PORT_ISC_INPUT_DISABLE_gc;
        portb_pintctrl[i] = PORT_ISC_INPUT_DISABLE_gc;
    }
}

static void led_init(void) {
    LED_PORT.OUTCLR = LED_BIT;
    LED_PORT.DIRSET = LED_BIT;
}

static void valve_init(void) {
    // Set valve pin low
    VALVE_PORT.OUTCLR = VALVE_BIT;
    // Set valve pin as output
    VALVE_PORT.DIRSET = VALVE_BIT;
}

static volatile uint16_t adc0_res;

ISR(ADC0_RESRDY_vect) {
    adc0_res = ADC0.RES;
}

static int16_t measure_temperature(void) {
    debug_finish();

    // select internal 1.1V reference
    VREF.CTRLA = VREF_ADC0REFSEL_1V1_gc;

    ADC0.CTRLB = ADC_SAMPNUM_ACC64_gc;
    ADC0.CTRLA = ADC_RUNSTBY_bm | ADC_RESSEL_10BIT_gc;

    uint32_t freq = F_CPU / 2;
    uint8_t presc = 0;
    while (freq > 100000) {
        ++presc;
        freq >>= 1;
    }
    presc = presc << ADC_PRESC_gp;
    ADC0.CTRLC = ADC_SAMPCAP_bm | ADC_REFSEL_INTREF_gc | presc;
    // select temperature sensor
    ADC0.MUXPOS = ADC_MUXPOS_TEMPSENSE_gc;
    // INITDLY > 32us * f_clk_adc
    ADC0.CTRLD = ADC_INITDLY_DLY256_gc;
    // SAMPLEN > 32us * f_clk_adc
    ADC0.SAMPCTRL = 8;

    adc0_res = 0;
    ADC0.INTCTRL = ADC_RESRDY_bm;
    // enable ADC0
    ADC0.CTRLA = ADC_RUNSTBY_bm | ADC_RESSEL_10BIT_gc | ADC_ENABLE_bm;

    // start measurement
    ADC0.COMMAND = ADC_STCONV_bm;

    cli();
    set_sleep_mode(SLEEP_MODE_STANDBY);
    sleep_enable();
    while (adc0_res == 0) {
        sei();
        sleep_cpu();
        cli();
    }
    sleep_disable();
    // disable ADC0
    ADC0.INTCTRL = 0;
    ADC0.CTRLA = 0;
    sei();

    int32_t offset = SIGROW.TEMPSENSE1 * 64;
    uint8_t gain = SIGROW.TEMPSENSE0;
    uint32_t temp = adc0_res;
    temp -= offset;
    temp *= gain;
    int32_t celsius = temp - (((int32_t)273) << 14);
    celsius >>= 10;

    return (int16_t)celsius;
}

static inline uint32_t measure_weight(void) {
    uint32_t w = 0;
    buckets_reset();
    hx711_start();
    for (uint8_t i = 0; i < 32; ++i) {
        uint32_t r = hx711_read();
        LOG("%lu\n", r);
        LED_PORT.OUTCLR = LED_BIT;
        buckets_add(r);
        LED_PORT.OUTSET = LED_BIT;
        buckets_dump();
        w += r;
    }
    LOG("f: %lu\n", buckets_filter());
    w >>= 5;
    hx711_powerdown();
    return w;
}

static inline void open_valve(void) {
    VALVE_PORT.OUTSET = VALVE_BIT;
}

static inline void close_valve(void) {
    VALVE_PORT.OUTCLR = VALVE_BIT;
}

static inline void start_watchdog(void) {
    // wait for any pending WDT sync
    while ((WDT.STATUS & WDT_SYNCBUSY_bm) != 0)
        ;

    LOCKI();
    wdt_enable(WDT_PERIOD_8KCLK_gc);
    UNLOCKI();
}

static void shutdown(uint8_t mode) {
    if (mode == SLEEP_MODE_IDLE) {
        debug_finish();
    } else {
        debug_stop();
    }
    hx711_await_poweroff();
    cli();
    if (mode == SLEEP_MODE_STANDBY) {
        debug_prepare_standby();
    }

    LED_PORT.OUTCLR = LED_BIT;

    // stop watchdog
    wdt_disable();
    set_sleep_mode(mode);
    sleep_enable();
    sei();
    sleep_cpu();
    sleep_disable();
    // restart watchdog
    start_watchdog();
    debug_init();
}

static void wait_for_input(void) {
    cli();
    set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_enable();
    while (!debug_char_pending()) {
        sei();
        sleep_cpu();
        cli();
    }
    sleep_disable();
    sei();
}

static void loop(void) {
    for (;;) {
        wdt_reset();
        LOGS("Waiting for command\n");
        wait_for_input();
        wdt_reset();
        LED_PORT.OUTSET = LED_BIT;
        char cmd = debug_getchar();
        switch (cmd) {
        case 't': {
            LOGS("Measuring temperature: ");
            int16_t t = measure_temperature();
            int16_t i = t >> 4;
            uint8_t f = (((t > 0 ? t : -t) & 0xF) * 10) >> 4;
            LOG("%d.%d\n", i, f);
            break;
        }
        case 'w': {
            LOG("Measuring weight:\n");
            uint32_t w = measure_weight();
            LOG("Weight: %lu\n", w);
            break;
        }
        case 's': {
            LOGS("Standby\n");
            shutdown(SLEEP_MODE_STANDBY);
            break;
        }
        case 'p': {
            LOGS("Powerdown\n");
            shutdown(SLEEP_MODE_PWR_DOWN);
            break;
        }
        case 'v': {
            LOGS("Version: " VERSION_STR "\n");
            break;
        }
        default: {
            LOG("Invalid command: '%c'\n", cmd);
            break;
        }
        }
        LED_PORT.OUTCLR = LED_BIT;
    }
}

int main(void) {
    // save reset reason
    uint8_t rstfr = RSTCTRL.RSTFR;
    // clear reset flags for next reset
    RSTCTRL.RSTFR = RSTCTRL_PORF_bm | RSTCTRL_BORF_bm | RSTCTRL_EXTRF_bm |
                    RSTCTRL_WDRF_bm | RSTCTRL_SWRF_bm | RSTCTRL_UPDIRF_bm;

    early_init();

    wdt_disable();

    // init
    {
        valve_init();
        led_init();
        hx711_init();
        debug_init();
        buckets_init(1);
        sei();

        debug_dump_trace();
        debug_init_trace();
    }

    LOG("\nreset: %#x\n", rstfr);
    start_watchdog();
    loop();

    return 0; /* never reached */
}
