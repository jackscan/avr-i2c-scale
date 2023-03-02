/*
    SPDX-FileCopyrightText: 2023 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#include "util.h"

void write_big_endian_u16(uint8_t *dst, uint16_t v) {
    dst[0] = v >> 8;
    dst[1] = v & 0xFF;
}

void write_big_endian_u32(uint8_t *dst, uint32_t v) {
    write_big_endian_u16(dst, v >> 16);
    write_big_endian_u16(dst+2, v & 0xFFFF);
}

void read_big_endian_u16(uint16_t *dst, uint8_t *src){
    *dst = (((uint16_t)src[0]) << 8) | (uint16_t)src[1];
}

void read_big_endian_u32(uint32_t *dst, uint8_t *src){
    read_big_endian_u16(((uint16_t*)dst) + 1, src);
    read_big_endian_u16(((uint16_t*)dst) + 0, src + 2);
}
