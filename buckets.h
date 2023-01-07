/*
    SPDX-FileCopyrightText: 2023 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>

void buckets_init(uint8_t min_shift);
void buckets_reset(void);
bool buckets_empty(void);
void buckets_add(uint32_t val);
uint32_t buckets_filter(void);
void buckets_dump(void);
