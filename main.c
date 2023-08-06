/*
    SPDX-FileCopyrightText: 2022 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#include "buckets.h"
#include "config.h"
#include "debug.h"
#include "hx711.h"
#include "nvm.h"
#include "timer.h"
#include "twi.h"
#include "version.h"

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

static bool wd_disabled = false;

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
    LED2_PORT.OUTCLR = LED2_BIT;
    LED2_PORT.DIRSET = LED2_BIT;
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
    set_sleep_mode(SLEEP_MODE_IDLE);
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

    int32_t offset = (int8_t)SIGROW.TEMPSENSE1 * 64L;
    uint8_t gain = SIGROW.TEMPSENSE0;
    uint32_t temp = adc0_res;
    temp -= offset;
    temp *= gain;
    temp >>= 10;
    return temp - 4370; // Celsius * 16 = K * 16 - 273.15 * 16
}

static inline uint32_t calculate_weight(uint32_t result) {
    if (result < calib_data.hx711.offset) {
        return 0UL;
    }
    uint16_t s = calib_data.hx711.scale;
    uint32_t r = result - calib_data.hx711.offset;
    uint32_t a = r * (uint8_t)(s >> 8);
    uint32_t b = r * (uint8_t)(s & 0xff) / 256UL;
    return (a + b) / 256UL;
}

static inline void open_valve(void) {
    LED2_PORT.OUTSET = LED2_BIT;
    VALVE_PORT.OUTSET = VALVE_BIT;
}

static inline void close_valve(void) {
    VALVE_PORT.OUTCLR = VALVE_BIT;
    LED2_PORT.OUTCLR = LED2_BIT;
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
    close_valve();
    timer_stop();

    // stop watchdog
    wdt_disable();
    set_sleep_mode(mode);
    sleep_enable();
    sei();
    sleep_cpu();
    sleep_disable();
    if (!wd_disabled) {
        // restart watchdog
        start_watchdog();
    }
    debug_init();
}

static void wait_for_input(void) {
    cli();
    set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_enable();
    while (!twi_task_pending() && !hx711_is_data_available() &&
           !debug_char_pending()) {
        sei();
        sleep_cpu();
        cli();
        if (twi_busy()) {
            wdt_reset();
        }
    }
    sleep_disable();
    sei();
}

struct twi_data twi_data = {.task = TWI_CMD_NONE, .count = 0};

static bool expect_twi_data(uint8_t count) {
    if (twi_data.count != count) {
        LOGHEX(twi_data.task);
        LOGS(": invalid: ");
        LOGDEC(twi_data.count);
        LOGNL();
        return false;
    }
    return true;
}

static void start_hx711(void) {
    LED_PORT.OUTSET = LED_BIT;
    hx711_start();
}

static void loop(void) {
    for (;;) {
        LOGS("> ");
        wait_for_input();
        if (twi_task_pending() || debug_char_pending()) {
            wdt_reset();
        }
        if (debug_char_pending()) {
            char cmd = debug_getchar();
            switch (cmd) {
            case 's': {
                LOGS("Stdby\n");
                shutdown(SLEEP_MODE_STANDBY);
                break;
            }
            case 'd': twi_dump_dbg(); break;
            case 't': {
                int16_t t = measure_temperature();
                int16_t i = t >> 4;
                uint8_t f = (((t > 0 ? t : -t) & 0xF) * 10) >> 4;
                LOGS("TEMP: ");
                LOGDEC(i);
                LOGC('.');
                LOGDEC(f);
                LOGNL();
                break;
            }
            default: {
                LOGS("Invalid: '");
                LOGC(cmd);
                LOGS("'\n");
                break;
            }
            }
        }
        if (twi_task_pending()) {
            twi_read(&twi_data);
            switch (twi_data.task) {
            case TWI_CMD_SLEEP:
                LOGS("SLP\n");
                shutdown(SLEEP_MODE_PWR_DOWN);
                break;
            case TWI_CMD_TRACK_WEIGHT:
                timer_start();
                if (!hx711_is_active()) {
                    start_hx711();
                    LOGS("TRACK\n");
                }
                break;
            case TWI_CMD_MEASURE_WEIGHT:
                buckets_reset();
                LOGS("MEAS\n");
                if (!hx711_is_active()) {
                    start_hx711();
                }
                break;
            case TWI_CMD_GET_TEMP: {
                int16_t t = measure_temperature();
                uint8_t d[2];
                write_big_endian_u16(d, t);
                twi_write(sizeof(d), d);
                int16_t i = t >> 4;
                uint8_t f = (((t > 0 ? t : -t) & 0xF) * 10) >> 4;
                LOGS("TEMP: ");
                LOGDEC(i);
                LOGC('.');
                LOGDEC(f);
                LOGNL();
                break;
            }
            case TWI_CMD_OPEN_VALVE:
                open_valve();
                break;
            case TWI_CMD_CLOSE_VALVE:
                close_valve();
                break;
            case TWI_CMD_DISABLE_WD:
                if (expect_twi_data(1) && twi_data.buf[0] == TWI_CONFIRM_DISABLE_WD) {
                    LOGS("WD_OFF");
                    wdt_disable();
                    wd_disabled = true;
                }
                break;
            case TWI_CMD_ENABLE_WD:
                if (wd_disabled) {
                    LOGS("WD_ON");
                    start_watchdog();
                    wd_disabled = false;
                }
                break;
            case TWI_CMD_GET_CALIB: {
                uint8_t d[6];
                write_big_endian_u32(d, calib_data.hx711.offset);
                write_big_endian_u16(d + 4, calib_data.hx711.scale);
                twi_write(sizeof(d), d);
                LOGS("GCALIB: ");
                LOGDEC_U32(calib_data.hx711.offset);
                LOGS(", ");
                LOGDEC_U16(calib_data.hx711.scale);
                LOGNL();
                break;
            }
            case TWI_CMD_SET_CALIB:
                if (expect_twi_data(6)) {
                    read_big_endian_u32(&calib_data.hx711.offset, twi_data.buf);
                    read_big_endian_u16(&calib_data.hx711.scale,
                                        twi_data.buf + 4);
                    LOGS("SCALIB: ");
                    LOGDEC_U32(calib_data.hx711.offset);
                    LOGS(", ");
                    LOGDEC_U16(calib_data.hx711.scale);
                    LOGNL();
                }
                break;
            case TWI_CMD_CALIB_WRITE:
                if (expect_twi_data(1) &&
                    twi_data.buf[0] == TWI_CONFIRM_CALIB_WRITE) {
                    nvm_write_calib_data();
                    LOGS("WCALIB\n");
                }
                break;
            case TWI_CMD_SET_ADDR:
                if (expect_twi_data(1)) {
                    twi_addr = twi_data.buf[0];
                }
                break;
            case TWI_CMD_ADDR_WRITE:
                if (expect_twi_data(1) &&
                    twi_data.buf[0] == TWI_CONFIRM_ADDR_WRITE) {
                    nvm_write_twi_addr();
                    LOGS("Addr\n");
                }
                break;
            }

            if (twi_data.task != TWI_CMD_MEASURE_WEIGHT &&
                twi_data.task != TWI_CMD_TRACK_WEIGHT && hx711_is_active()) {
                hx711_powerdown();
                LED_PORT.OUTCLR = LED_BIT;
                timer_stop();
            }
        }
        if (hx711_is_data_available()) {
            uint32_t d = hx711_read();
            uint32_t w = calculate_weight(d);
            LOGS("w: ");
            LOGDEC_U32(w);
            LOGS(" (");
            LOGDEC_U32(d);
            LOGS(")\n");
            if (twi_data.task == TWI_CMD_TRACK_WEIGHT) {
                uint16_t rt = timer_get_time();
                uint8_t t = rt * 250U / 256;
                // uint16_t t = timer_get_time_ms();
                uint8_t data[6] = {
                    (w >> 24) & 0xff,
                    (w >> 16) & 0xff,
                    (w >> 8) & 0xff,
                    w & 0xff,
                    0,
                    t & 0xFF,
                };
                twi_write(6, data);
                LOGS("t: ");
                LOGDEC(t);
                LOGS(", ");
                LOGDEC_U16(rt);
                LOGNL();
            } else if (twi_data.task == TWI_CMD_MEASURE_WEIGHT) {
                buckets_add(w);
                // buckets_dump();
                accu_t r = buckets_filter();
                uint8_t data[7] = {
                    r.count,
                    (r.sum >> 24) & 0xff,
                    (r.sum >> 16) & 0xff,
                    (r.sum >> 8) & 0xff,
                    (r.sum) & 0xff,
                    r.total,
                    r.span,
                };
                twi_write(7, data);
                LOGS("c: ");
                LOGDEC_U32(r.sum);
                LOGS(", ");
                LOGDEC(r.count);
                LOGC('/');
                LOGDEC(r.total);
                LOGS(", ");
                LOGDEC(r.span);
                LOGNL();
            }
        }
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
        nvm_init();
        twi_init(twi_addr);
        timer_init();
        buckets_init(1);
        sei();

        debug_dump_trace();
        debug_init_trace();
    }

    LOGNL();
    LOGS("rst: ");
    LOGHEX(rstfr);
    LOGNL();
    LOGS("ADDR: ");
    LOGHEX(twi_addr);
    LOGNL();
    shutdown(SLEEP_MODE_STANDBY);
    loop();

    return 0; /* never reached */
}
