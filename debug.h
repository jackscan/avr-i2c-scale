/*
    SPDX-FileCopyrightText: 2022 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#pragma once

#include <stdbool.h>
#include <stdio.h>

#ifndef NO_SERIAL

#include "util.h"

void debug_init(void);
bool debug_char_pending(void);
char debug_getchar(void);
void debug_finish(void);
void debug_prepare_standby(void);
void debug_stop(void);

#define LOG(MSG, ...) printf_P(FSTR(MSG), ##__VA_ARGS__)
#define LOGS(MSG)     fputs_P(FSTR(MSG), stdout)
#define LOGC(C)       putchar(C)

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

inline static void ignore_log(void *m, ...) {}
#define LOG(MSG, ...) ignore_log(MSG, ##__VA_ARGS__)
#define LOG_P(MSG)    ignore_log(MSG)
#define LOGC(C)       ignore_log(C)

#endif

#if ENABLE_CHECKPOINTS
void debug_init_trace(void);
void debug_dump_trace(void);
void checkpoint(void);
#define CHECKPOINT checkpoint()
#else
#define CHECKPOINT do {} while (0)
static inline void debug_init_trace(void) {}
static inline void debug_dump_trace(void) {}
#endif
