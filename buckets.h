/*
    SPDX-FileCopyrightText: 2023 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct accu {
    uint32_t sum;
    uint8_t count;
    uint8_t shift;
} accu_t;

void buckets_init(uint8_t min_shift);
void buckets_reset(void);
bool buckets_empty(void);
void buckets_add(uint32_t val);
accu_t buckets_filter(void);
void buckets_dump(void);
