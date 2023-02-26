/*
    SPDX-FileCopyrightText: 2022 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#pragma once

#include "util.h"

#include <stdbool.h>
#include <stdio.h>

#ifndef NO_SERIAL

void debug_init(void);
bool debug_char_pending(void);
char debug_getchar(void);
void debug_putchar(char c);
void debug_puts_p(const __flash char *str);
void debug_putdec_u8(uint8_t u);
void debug_putdec_u16(uint16_t u);
void debug_putdec_u32(uint32_t u);
void debug_puthex(uint8_t u);
void debug_finish(void);
void debug_prepare_standby(void);
void debug_stop(void);

#define LOGS(MSG)     debug_puts_p(FSTR(MSG))
#define LOGC(C)       debug_putchar(C)
#define LOGNL()       debug_putchar('\n')
#define LOGHEX(N)     debug_puthex(N)
#define LOGDEC(N)     debug_putdec_u8(N)
#define LOGDEC_U16(N) debug_putdec_u16(N)
#define LOGDEC_U32(N) debug_putdec_u32(N)

#else

inline void debug_init(void) {}
inline bool debug_char_pending(void) {
    return false;
}
inline char debug_getchar(void) {
    return EOF;
}
inline void debug_finish(void) {}
inline void debug_prepare_standby(void) {}
inline void debug_stop(void) {}

inline static void ignore_p(void *m) {}
inline static void ignore_i(intptr_t i) {}

#define LOGS(MSG)     ignore_p(MSG)
#define LOGC(C)       ignore_i(C)
#define LOGNL()       ignore_i(0)
#define LOGHEX(N)     ignore_i(N)
#define LOGDEC(N)     ignore_i(N)
#define LOGDEC_U16(N) ignore_i(N)
#define LOGDEC_U32(N) ignore_i(N)

#endif

#if ENABLE_CHECKPOINTS
void debug_init_trace(void);
void debug_dump_trace(void);
void checkpoint(void);
#define CHECKPOINT checkpoint()
#else
#define CHECKPOINT                                                             \
    do {                                                                       \
    } while (0)
static inline void debug_init_trace(void) {}
static inline void debug_dump_trace(void) {}
#endif
