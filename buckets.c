/*
    SPDX-FileCopyrightText: 2023 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#include "buckets.h"

#include "debug.h"

#include <string.h>

#define BUCKET_COUNT 8

struct {
    uint32_t accu[BUCKET_COUNT];
    uint8_t count[BUCKET_COUNT];
    uint32_t base;
    uint8_t shift;
    int8_t lower;
    int8_t upper;
    uint8_t min_shift;
} buckets;

void buckets_init(uint8_t min_shift) {
    buckets.min_shift = min_shift;
}

void buckets_reset(void) {
    memset(buckets.accu, 0, sizeof(buckets.accu));
    memset(buckets.count, 0, sizeof(buckets.count));
    buckets.shift = 0;
    buckets.lower = 0;
    buckets.upper = 0;
}

bool buckets_empty(void) {
    return buckets.upper == 0;
}

static void buckets_merge(int8_t dst, int8_t src0) {
    buckets.accu[dst] = buckets.accu[src0] + buckets.accu[src0 + 1];
    buckets.count[dst] = buckets.count[src0] + buckets.count[src0 + 1];
}

void buckets_deflate(void) {
    for (int8_t i = 0, j = 0; i + 1 < buckets.upper; ++j, i += 2) {
        buckets_merge(j, i);
    }

    if ((buckets.upper & 0x1) != 0) {
        int8_t i = buckets.upper - 1;
        int8_t j = i >> 1;
        buckets.accu[j] = buckets.accu[i];
        buckets.count[j] = buckets.count[i];
    }

    for (int8_t i = BUCKET_COUNT - 1, j = i; i - 1 >= buckets.lower;
         --j, i -= 2) {
        buckets_merge(j, i - 1);
    }

    if ((buckets.lower & 0x1) != 0) {
        int8_t i = buckets.lower;
        int8_t j = (BUCKET_COUNT + i) >> 1;
        buckets.accu[j] = buckets.accu[i];
        buckets.count[j] = buckets.count[i];
    }

    ++buckets.shift;
    int8_t i = (buckets.upper + 1) >> 1;
    int8_t j = (BUCKET_COUNT + buckets.lower) >> 1;
    memset(buckets.accu + i, 0, sizeof(buckets.accu[0]) * (j - i));
    memset(buckets.count + i, 0, sizeof(buckets.count[0]) * (j - i));
    buckets.upper = i;
    buckets.lower = j;
}

void buckets_add(uint32_t val) {
    if (buckets_empty()) {
        buckets.shift = buckets.min_shift;
        buckets.base = val;
        buckets.upper = 1;
        buckets.lower = BUCKET_COUNT;
    }

    int8_t i;
    for (;;) {
        i = ((int32_t)val - (int32_t)buckets.base) >> buckets.shift;
        if ((i < 0 && i + BUCKET_COUNT >= buckets.upper) ||
            (i >= 0 && i < buckets.lower)) {
            break;
        }
        buckets_deflate();
    };

    if (i < 0) {
        i += BUCKET_COUNT;
        if (i < buckets.lower) {
            buckets.lower = i;
        }
    } else if (i >= buckets.upper) {
        buckets.upper = i + 1;
    }

    buckets.accu[i] += val;
    ++buckets.count[i];
}

static uint8_t buckets_start(uint8_t thresh) {
    uint8_t start = buckets.lower;
    for (; start < BUCKET_COUNT; ++start) {
        if (buckets.count[start] >= thresh) {
            return start;
        }
    }
    start = 0;
    while (buckets.count[start] < thresh) {
        ++start;
    }
    return start;
}

static uint8_t buckets_end(uint8_t thresh) {
    uint8_t end = buckets.upper;
    for (; end > 0; --end) {
        if (buckets.count[end - 1] >= thresh) {
            return end;
        }
    }
    end = BUCKET_COUNT;
    while (buckets.count[end - 1] < thresh) {
        --end;
    }
    return end;
}

accu_t buckets_filter(void) {
    uint8_t total = 0;
    for (uint8_t i = 0; i < BUCKET_COUNT; ++i) {
        total += buckets.count[i];
    }
    uint8_t thresh = total / BUCKET_COUNT;

    uint8_t start = buckets_start(thresh);
    uint8_t end = buckets_end(thresh);

    accu_t res = {
        .sum = 0,
        .count = 0,
        .total = total,
        .span = end - start - 1,
    };
    uint8_t i = start;
    if (end <= start) {
        for (; i < BUCKET_COUNT; ++i) {
            res.count += buckets.count[i];
            res.sum += buckets.accu[i];
        }
        i = 0;
        res.span += BUCKET_COUNT;
    }

    res.span |= buckets.shift << 3;

    for (; i < end; ++i) {
        res.count += buckets.count[i];
        res.sum += buckets.accu[i];
    }
    return res;
}

void buckets_dump(void) {
    LOGC('[');
    LOGDEC(buckets.upper);
    LOGS(", ");
    LOGDEC(buckets.lower);
    LOGS(", ");
    LOGDEC(buckets.shift);
    LOGS(", ");
    LOGDEC_U32(buckets.base);
    LOGS("]\n");

    for (int8_t i = 0; i < BUCKET_COUNT; ++i) {
        LOGDEC(buckets.count[i]);
        LOGS(", ");
        LOGDEC_U32(buckets.accu[i]);
        LOGNL();
    }
}
