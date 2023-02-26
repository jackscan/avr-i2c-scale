/*
    SPDX-FileCopyrightText: 2022 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#include "debug.h"

#include "config.h"
#include "util.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <string.h>

#ifndef NO_SERIAL

#define USART0_BAUD_RATE(R) ((uint16_t)((F_CPU * 64UL + 8UL * R) / (16UL * R)))

#define TX_BUFFER_SIZE 16
#define RX_BUFFER_SIZE 4

struct TxBuffer {
    uint8_t buf[TX_BUFFER_SIZE];
    volatile uint8_t head;
    uint8_t tail;
};

struct RxBuffer {
    volatile uint8_t buf[RX_BUFFER_SIZE];
    uint8_t head;
    volatile uint8_t tail;
};

static struct Serial {
    struct TxBuffer send;
    struct RxBuffer recv;
    volatile uint8_t rx_dropped;
    volatile uint8_t rx_errcnt;
    volatile bool tx_complete;
} serial;

ISR(USART0_RXC_vect) {
    if ((USART0.STATUS & USART_RXCIF_bm) != 0) {
        uint8_t newtail = (serial.recv.tail + 1) % RX_BUFFER_SIZE;
        uint8_t rxh = USART0.RXDATAH;
        uint8_t rxl = USART0.RXDATAL;

        bool err = (rxh & (USART_FERR_bm | USART_PERR_bm)) != 0;

        if ((rxh & USART_BUFOVF_bm) != 0) {
            serial.rx_dropped += 1;
        }

        if (err) {
            serial.rx_errcnt += 1;
        }

        if (newtail != serial.recv.head && !err) {
            serial.recv.buf[serial.recv.tail] = rxl;
            serial.recv.tail = newtail;
        } else {
            serial.rx_dropped += 1;
        }
    } else if ((USART0.STATUS & USART_RXSIF_bm) != 0) {
        // start of frame detected
        // disable detection
        USART0.CTRLB &= ~USART_SFDEN_bm;
        // reset interrupt flag
        USART0.STATUS = USART_RXSIF_bm;
    }
}

ISR(USART0_DRE_vect) {
    if (serial.send.head != serial.send.tail) {
        // clear interrupt flag for TXC
        USART0.STATUS = USART_TXCIF_bm;
        // enable TXC interrupt
        USART0.CTRLA |= USART_TXCIE_bm;
        serial.tx_complete = false;
        USART0.TXDATAL = serial.send.buf[serial.send.head];
        serial.send.head = (serial.send.head + 1) % TX_BUFFER_SIZE;
    } else {
        // nothing to transmit, disable interrupt
        USART0.CTRLA &= ~USART_DREIE_bm;
    }
}

ISR(USART0_TXC_vect) {
    serial.tx_complete = true;
    // disable interrupt
    USART0.CTRLA &= ~USART_TXCIE_bm;
    // clear interrupt flag
    USART0.STATUS = USART_TXCIF_bm;
}

void debug_init(void) {
    LOCKI();
    RXD_PINCTRL = PORT_ISC_INTDISABLE_gc;
    TXD_PORT.DIRSET = TXD_BIT;
    RXD_PORT.DIRCLR = RXD_BIT;
    USART0.BAUD = USART0_BAUD_RATE(BAUDRATE);
    USART0.CTRLC = USART_CMODE_ASYNCHRONOUS_gc | USART_PMODE_DISABLED_gc |
                   USART_SBMODE_1BIT_gc | USART_CHSIZE_8BIT_gc;
    USART0.CTRLA =
        USART_RXCIE_bm | USART_TXCIE_bm | USART_DREIE_bm | USART_RXSIE_bm;
    USART0.CTRLB = USART_TXEN_bm | USART_RXEN_bm;
    UNLOCKI();
}

inline bool debug_char_pending(void) {
    return serial.recv.head != serial.recv.tail;
}

char debug_getchar(void) {
    char c = EOF;
    if (serial.recv.head != serial.recv.tail) {
        c = serial.recv.buf[serial.recv.head];
        serial.recv.head = (serial.recv.head + 1) % RX_BUFFER_SIZE;
    }
    return c;
}

static void dbg_wait_tx(uint8_t tail) {
    cli();
    set_sleep_mode(SLEEP_MODE_IDLE);
    while (tail == serial.send.head) {
        sleep_enable();
        sei();
        sleep_cpu();
        sleep_disable();
        cli();
    }
    sei();
}

static inline void dbg_push_tail(uint8_t tail) {
    cli();
    USART0.CTRLA |= USART_DREIE_bm;
    serial.tx_complete = false;
    serial.send.tail = tail;
    sei();
}

void debug_putchar(char c) {
    uint8_t newtail = (serial.send.tail + 1) % TX_BUFFER_SIZE;
    if (newtail == serial.send.head) {
        dbg_wait_tx(newtail);
    }

    serial.send.buf[serial.send.tail] = c;
    dbg_push_tail(newtail);
}

void debug_puts_p(const __flash char *str) {
    for (const __flash char *c = str; *c != '\0'; ++c) {
        debug_putchar(*c);
    }
}

static void putdec_digit(uint8_t d) {
    debug_putchar('0' + d);
}

static void putdec_u32(uint32_t u, uint32_t d) {
    bool prefix = true;
    for (uint32_t i = d; i > 9; i /= 10) {
        uint8_t c = 0;
        while (u >= i) {
            u -= i;
            ++c;
        }
        if (c == 0 && prefix) {
            continue;
        }

        prefix = false;
        putdec_digit(c);
    }

    putdec_digit(u);
}

void debug_putdec_u8(uint8_t u) {
    putdec_u32(u, 100);
}

void debug_putdec_u16(uint16_t u) {
    putdec_u32(u, 10000);
}

void debug_putdec_u32(uint32_t u) {
    putdec_u32(u, 1000000000UL);
}

static void puthex_digit(uint8_t h) {
    if (h < 0xA) {
        debug_putchar('0' + h);
    } else {
        debug_putchar('A' - 0xA + h);
    }
}

void debug_puthex(uint8_t u) {
    debug_putchar('0');
    debug_putchar('x');
    puthex_digit(u >> 4);
    puthex_digit(u & 0xF);
}

void debug_write(const char *str, uint8_t len) {
    while (len > 0) {
        uint8_t n = (serial.send.head + TX_BUFFER_SIZE - serial.send.tail - 1) %
                    TX_BUFFER_SIZE;
        if (n == 0) {
            dbg_wait_tx((serial.send.tail + 1) % TX_BUFFER_SIZE);
            continue;
        }

        if (n > len) {
            n = len;
        }

        if (serial.send.tail + n >= TX_BUFFER_SIZE) {
            uint8_t c = TX_BUFFER_SIZE - serial.send.tail;
            memcpy(serial.send.buf + serial.send.tail, str, c);
            str += c;
            len -= c;
            n -= c;
            serial.send.tail = 0;
        }

        memcpy(serial.send.buf, str, n);
        dbg_push_tail(serial.send.tail + n);

        str += n;
        len -= n;
    }
}

void debug_finish(void) {
    cli();
    set_sleep_mode(SLEEP_MODE_IDLE);
    while (!serial.tx_complete) {
        sleep_enable();
        sei();
        sleep_cpu();
        sleep_disable();
        cli();
    }
    sei();
}

void debug_stop(void) {
    debug_finish();
    // disable all interrupts
    USART0.CTRLA = 0;
    // From errata:
    // Make sure the receiver is enabled while disabling the transmitter.
    USART0.CTRLB = USART_RXEN_bm;
    USART0.CTRLB = 0;
}

void debug_prepare_standby(void) {
    // Enable start-of-frame detection
    USART0.CTRLB = USART_SFDEN_bm | USART_RXEN_bm;
    // Enable RX interrupts
    USART0.CTRLA = USART_RXSIE_bm | USART_RXCIE_bm;
}

#endif

#if ENABLE_CHECKPOINTS

#define TRACE_LEN 32
static struct {
    uint8_t index;
    uint16_t addr[TRACE_LEN];
} s_trace __attribute__((section(".noinit")));

void __attribute__((noinline, naked)) checkpoint(void) {
    __asm__ __volatile__(
        "pop r31"
        "\n\t" // pop return address into Z
        "pop r30"
        "\n\t"
        "ldi r26, lo8(%[idx])"
        "\n\t" // X = &s_trace
        "ldi r27, hi8(%[idx])"
        "\n\t"
        "ld r18, X"
        "\n\t" // tmp = s_trace.index
        "subi r18, -2"
        "\n\t" // tmp += 2
        "andi r18, %[msk]"
        "\n\t" // tmp &= msk
        "st X+, r18"
        "\n\t" // s_trace.index = tmp, X = s_trace.addr
        "add r26, r18"
        "\n\t" // X += tmp
        "adc r27, __zero_reg__"
        "\n\t"
        "st X+, r30"
        "\n\t"
        "st X, r31"
        "\n\t"
        "ijmp"
        "\n\t"
        :
        : [idx] "i"(&s_trace.index), [msk] "i"((2 * TRACE_LEN) - 1)
        :);
}

#endif

#if ENABLE_CHECKPOINTS
void debug_init_trace(void) {
    memset(&s_trace, 0, sizeof(s_trace));
}

void debug_dump_trace(void) {
    if ((s_trace.index & 1) == 0 && s_trace.index < sizeof(s_trace.addr)) {
        LOG("trace:");
        uint8_t i = s_trace.index / 2;
        do {
            i = (i + 1) % TRACE_LEN;
            LOG(" %#x", s_trace.addr[i] * 2);
        } while (i * 2 != s_trace.index);
        LOG("\n");
    }
}
#endif
