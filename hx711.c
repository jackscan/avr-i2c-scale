/*
    SPDX-FileCopyrightText: 2022 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#include "hx711.h"

#include "config.h"
#include "debug.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sleep.h>

#define SPI_OFF (SPI_MASTER_bm | SPI_PRESC_DIV16_gc)
#define SPI_ON (SPI_OFF | SPI_ENABLE_bm)

struct {
    volatile uint32_t data;
    volatile enum {
        HX711_INIT,
        HX711_TX_STARTED,
        HX711_1ST_BYTE_RECEIVED,
        HX711_TX_COMPLETE,
        HX711_POWERING_DOWN,
        HX711_OFF,
    } state;
} hx711;

ISR(MISO_PORT_VECT) {
    // Check interrupt flag on MISO pin (configured for sensing falling edge)
    if ((MISO_PORT.INTFLAGS & MISO_BIT) != 0) {
        // Disable interrupt on MISO pin
        MISO_PINCTRL = PORT_ISC_INTDISABLE_gc;
        // Enable SPI interrupt for "Receive Complete"
        SPI0.INTCTRL = SPI_RXCIE_bm;
        // Enqueue first two bytes to start SPI transfer
        SPI0.DATA = 0;
        SPI0.DATA = 0;
        hx711.state = HX711_TX_STARTED;
    }
}

ISR(SPI0_INT_vect) {
    uint8_t *d = (uint8_t*)&hx711.data;
    if ((SPI0.INTFLAGS & SPI_RXCIF_bm) != 0 &&
        hx711.state == HX711_TX_STARTED) {
        // SPI received first byte
        // Read first byte of 24bit into MSB
        d[2] = SPI0.DATA ^ 0x80;
        hx711.state = HX711_1ST_BYTE_RECEIVED;
        // Enable interrupt for TX complete and disable for RX complete
        SPI0.INTCTRL = SPI_TXCIE_bm;
        // Enqueue third and last byte for SPI transfer
        SPI0.DATA = 0;
        // Clear interrupt flag for TX complete
        SPI0.INTFLAGS = SPI_TXCIF_bm;
    } else if ((SPI0.INTFLAGS & SPI_TXCIF_bm) != 0 &&
               hx711.state == HX711_1ST_BYTE_RECEIVED) {
        // SPI transfer complete
        // Read second and third byte
        d[1] = SPI0.DATA;
        d[0] = SPI0.DATA;
        // HX711 requires 25 pulses on SCK. So far there were 24 pulses for
        // three bytes of SPI transfer.
        // Disabling SPI will activate PORT settings of SCK,
        // which was set to output and high in hx711_init.
        // By shortly disabling SPI we generate the 25th pulse on SCK.
        SPI0.CTRLA = SPI_OFF;
        // Disable SPI interrupts
        SPI0.INTCTRL = 0;
        hx711.state = HX711_TX_COMPLETE;
        // Reenabling SPI will drive SCK low again.
        SPI0.CTRLA = SPI_ON;
    }
}

ISR(TCB0_INT_vect) {
    if ((TCB0.INTFLAGS & TCB_CAPT_bm) != 0) {
        hx711.state = HX711_OFF;
        // Stop timer
        TCB0.CTRLA = 0;
        // Disable interrupt
        TCB0.INTCTRL = TCB_CAPT_bm;
        // Clear interrupt flag
        TCB0.INTFLAGS = TCB_CAPT_bm;
    }
}

void hx711_init(void) {
    // Start powerdown timer to ensure hx711 has been off
    // before next read is started.
    hx711_powerdown();
    // Drive SCK high.
    // This takes effect when SPI is disabled.
    SCK_PORT.OUTSET = SCK_BIT;
    SCK_PORT.DIRSET = SCK_BIT;

    // Setup SPI as master with prescaler DIV16, but leaving it disabled.
    SPI0.CTRLA = SPI_OFF;
    // Enable Buffer mode, disable client select, select SPI mode 1.
    // SPI Mode 1: SCK is initially low, and Data is sampled on falling edge
    SPI0.CTRLB = SPI_BUFEN_bm | SPI_SSD_bm | SPI_MODE_1_gc;
}

uint32_t hx711_read(void) {
    if (hx711.state == HX711_POWERING_DOWN) {
        hx711_await_poweroff();
    }

    hx711.data = 0;
    hx711.state = HX711_INIT;

    cli();
    // Clear interrupt flag for MISO pin
    MISO_PORT.INTFLAGS = MISO_BIT;
    // Enable interrupt for MISO pin sensing falling edge
    MISO_PINCTRL = PORT_ISC_FALLING_gc;
    // Enable SPI.
    // This will drive SCK low which will power up the HX711
    SPI0.CTRLA = SPI_ON;
    // Clear interrupt flag for RX complete
    SPI0.INTFLAGS = SPI_RXCIF_bm;

    // Sleep while waiting for data to be ready and transfer to complete
    set_sleep_mode(SLEEP_MODE_IDLE);
    while (hx711.state != HX711_TX_COMPLETE) {
        sleep_enable();
        sei();
        sleep_cpu();
        sleep_disable();
        cli();
    }
    sei();

    return hx711.data;
}

void hx711_powerdown(void) {
    if (hx711.state < HX711_POWERING_DOWN) {
        hx711.state = HX711_POWERING_DOWN;
        // Disable SPI interrupts
        SPI0.INTCTRL = 0;
        // Disable SPI.
        // This will drive SCK high which will power off HX711 after 60us
        SPI0.CTRLA &= ~SPI_ENABLE_bm;
        // Disable digital input on MISO pin
        MISO_PINCTRL = PORT_ISC_INPUT_DISABLE_gc;

        // Start timer to wait 60us for hx711 to enter power down mode
        // Periodic interrupt mode
        TCB0.CTRLB = TCB_CNTMODE_INT_gc;
        // Calculate timer ticks for 60us
        const uint16_t ticks = (uint16_t)((F_CPU * 60.) / 2000000.);
        // Set TOP for 60us
        TCB0.CCMP = ticks;
        // Clear capture interrupt flag
        TCB0.INTFLAGS = TCB_CAPT_bm;
        // Enable capture interrupt
        TCB0.INTCTRL = TCB_CAPT_bm;
        // Start timer with CLKDIV2, and run in standby
        TCB0.CTRLA = TCB_RUNSTDBY_bm | TCB_CLKSEL_CLKDIV2_gc | TCB_ENABLE_bm;
    }
}

void hx711_await_poweroff(void) {
    if (hx711.state != HX711_OFF) {
        hx711_powerdown();
    }

    cli();
    set_sleep_mode(SLEEP_MODE_STANDBY);
    while (hx711.state != HX711_OFF) {
        sleep_enable();
        sei();
        sleep_cpu();
        sleep_disable();
        cli();
    }
    sei();
}
