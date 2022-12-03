/*
    SPDX-FileCopyrightText: 2021 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#pragma once

#include <avr/io.h>
#include <avr/pgmspace.h>

#include <util/delay.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define ARRAY_LEN(A) (sizeof(A) / sizeof(A[0]))

#define LOCKI()                                                                \
    uint8_t sreg = SREG;                                                       \
    cli()
#define RELOCKI()                                                              \
    sreg = SREG;                                                               \
    cli()
#define UNLOCKI() SREG = sreg

#define FSTR(str)                                                              \
    ({                                                                         \
        static const __flash char s[] = str;                                   \
        &s[0];                                                                 \
    })

static inline void setpin(volatile uint8_t *port, uint8_t pin, bool value)
    __attribute((always_inline));

static inline void setpin(volatile uint8_t *port, uint8_t pin, bool value) {
    if (value)
        *port |= (1 << pin);
    else
        *port &= ~(1 << pin);
}

static inline char *strprefix(char *str, const __flash char *prefix) {
    const uint8_t n = strlen_P(prefix);
    if (strncmp_P(str, prefix, n))
        return NULL;

    return str + n;
}

#define GET_SP()                                                               \
    ({                                                                         \
        void *temp;                                                            \
        __asm__ __volatile__("in %A0, %A1"                                     \
                             "\n\t"                                            \
                             "in %B0, %B1"                                     \
                             "\n\t"                                            \
                             : "=r"(temp)                                      \
                             : "I"((_SFR_IO_ADDR(SP))));                       \
        temp;                                                                  \
    })
