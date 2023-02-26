/*
    SPDX-FileCopyrightText: 2023 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#include "twi.h"

#include "config.h"
#include "debug.h"
#include "nvm.h"
#include "timer.h"
#include "util.h"
#include "version.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <string.h>

#define ACK  (TWI_ACKACT_ACK_gc | TWI_SCMD_RESPONSE_gc)
#define NACK (TWI_ACKACT_NACK_gc | TWI_SCMD_COMPTRANS_gc)
#define DONE TWI_SCMD_COMPTRANS_gc

enum {
    IDLE,
    STARTED,
    IN_PROGRESS,
};

struct {
    uint8_t cmd;
    uint8_t task;
    uint8_t index;
    uint8_t count;
    uint8_t crc;
    uint8_t buf[TWI_BUFFER_SIZE];
    uint8_t state;
    bool blocked;
    bool loaded;
    bool busy;
} twi = {.cmd = TWI_CMD_NONE, .task = TWI_CMD_NONE};

#ifndef NDEBUG

#define DBG_SIZE 16

struct {
    uint8_t index;
    struct {
        uint8_t status;
        uint8_t state_index;
        uint8_t crc;
    } buf[DBG_SIZE];
} twi_dbg;
#endif

#pragma pack(push, 1)
static const __flash struct {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    uint16_t hash;
} version_info = {
    .major = VERSION_MAJOR,
    .minor = VERSION_MINOR,
    .patch = GIT_DIRTY ? 0x80 | VERSION_PATCH : VERSION_PATCH,
    .hash = GIT_HASH,
};
#pragma pack(pop)

_Static_assert(sizeof(twi.buf) > sizeof(calib_data),
               "Two wire interface buffer is too small");

/// 4-bit lookup table for CRC-5-ITU with polynom 0x15, ref-in, ref-out
static const __flash uint8_t crc_table[16] = {
    0x00, 0x0d, 0x1a, 0x17, 0x1f, 0x12, 0x05, 0x08,
    0x15, 0x18, 0x0f, 0x02, 0x0a, 0x07, 0x10, 0x1d};

/// Update CRC-5-ITU for next byte.
static inline void twi_update_crc(uint8_t val) {
    uint8_t crc = twi.crc;
    crc = crc_table[(crc ^ val) & 0x0f] ^ (crc >> 4);
    twi.crc = crc_table[(crc ^ (val >> 4)) & 0x0f] ^ (crc >> 4);
}

static inline void prepare_recv(void) {
    switch (twi.cmd) {
    case TWI_CMD_SET_CALIB: twi.count = sizeof(calib_data); break;
    case TWI_CMD_CALIB_WRITE: // fallthrough
    case TWI_CMD_SET_ADDR:    // fallthrough
    case TWI_CMD_ADDR_WRITE:  // fallthrough
    case TWI_CMD_DISABLE_WD: twi.count = 1; break;
    default: twi.count = 0; break;
    }
}

static void finish_recv(void) {
    TWI0.SCTRLB = NACK;
    twi.state = IDLE;

    switch (twi.cmd) {
    case TWI_CMD_GET_VERSION:
        twi.task = TWI_CMD_NONE;
        twi.count = sizeof(version_info);
        memcpy_P(twi.buf, (const __flash uint8_t *)&version_info,
                 sizeof(version_info));
        twi.loaded = true;
        break;
    case TWI_CMD_OPEN_VALVE:                       // fallthrough
    case TWI_CMD_CLOSE_VALVE:                      // fallthrough
    case TWI_CMD_ENABLE_WD:                        // fallthrough
    case TWI_CMD_DISABLE_WD:                       // fallthrough
    case TWI_CMD_SET_ADDR:                         // fallthrough
    case TWI_CMD_ADDR_WRITE:                       // fallthrough
    case TWI_CMD_SET_CALIB:                        // fallthrough
    case TWI_CMD_CALIB_WRITE: twi.blocked = true;  // fallthrough
    default: twi.task = twi.cmd; break;
    }
}

static void prepare_send(void) {
    switch (twi.cmd) {
    case TWI_CMD_TRACK_WEIGHT:
        // Update current time delta
        if (twi.loaded) {
            uint8_t t = timer_get_time_ms() - twi.buf[5];
            twi.buf[4] = t;
            twi.count = 5;
        }
        break;
    default: break;
    }
}

ISR(TWI0_TWIS_vect) {
    uint8_t status = TWI0.SSTATUS;
    if ((status & TWI_APIF_bm) != 0) {
        if ((status & TWI_AP_bm) == TWI_AP_ADR_gc) {
            // Address
            if ((status & TWI_DIR_bm) != 0 && twi.loaded) {
                // Master read
                TWI0.SCTRLB = ACK;
                prepare_send();
                twi.state = STARTED;
                twi.index = 0;
                twi.crc = 0;
                twi.busy = true;
            } else if ((status & TWI_DIR_bm) == 0 && !twi.blocked) {
                // Master write
                TWI0.SCTRLB = ACK;
                twi.state = STARTED;
                twi.index = 0;
                twi.loaded = false;
                twi.busy = true;
            } else {
                TWI0.SCTRLB = NACK;
                twi.state = IDLE;
            }
        } else {
            // Stop
            TWI0.SCTRLB = DONE;
            twi.state = IDLE;
        }
    } else if ((status & TWI_DIF_bm) != 0) {
        if ((status & TWI_DIR_bm) != 0) {
            // Send byte
            if (((status & TWI_RXACK_bm) != 0 && twi.state != STARTED) ||
                twi.index > twi.count) {
                TWI0.SSTATUS = TWI_DIF_bm;
                TWI0.SCTRLB = DONE;
                twi.state = IDLE;
            } else if (twi.index == twi.count) {
                TWI0.SDATA = twi.crc & 0x1f;
                TWI0.SCTRLB = TWI_SCMD_RESPONSE_gc;
                ++twi.index;
            } else {
                uint8_t d = twi.buf[twi.index];
                TWI0.SDATA = d;
                TWI0.SCTRLB = TWI_SCMD_RESPONSE_gc;
                twi_update_crc(d);
                ++twi.index;
                twi.state = IN_PROGRESS;
            }
        } else if (twi.state == STARTED) {
            // Recv first byte
            twi.cmd = TWI0.SDATA;
            prepare_recv();
            if (twi.count > 0) {
                TWI0.SCTRLB = ACK;
                twi.task = TWI_CMD_NONE;
                twi.state = IN_PROGRESS;
            } else {
                // command without data
                finish_recv();
            }
        } else if (twi.index < twi.count) {
            // Recv data
            twi.buf[twi.index] = TWI0.SDATA;
            ++twi.index;
            if (twi.index < twi.count) {
                TWI0.SCTRLB = ACK;
            } else {
                // recv finished
                finish_recv();
            }
        } else {
            // should never happen
            TWI0.SCTRLB = NACK;
            twi.state = IDLE;
        }
    } else {
        TWI0.SCTRLB = NACK;
        twi.state = IDLE;
    }

#ifndef NDEBUG
    if (twi_dbg.index < DBG_SIZE) {
        twi_dbg.buf[twi_dbg.index].status = status;
        twi_dbg.buf[twi_dbg.index].state_index = (twi.state << 6) | twi.index;
        twi_dbg.buf[twi_dbg.index].crc = twi.crc;
        ++twi_dbg.index;
    }
#endif
}

#ifndef NDEBUG
void twi_dump_dbg(void) {
    LOGS("TWI:\n");
    for (uint8_t i = 0; i < twi_dbg.index; i++) {
        LOGHEX(twi_dbg.buf[i].status);
        LOGS(", ");
        switch (twi_dbg.buf[i].state_index >> 6) {
        case IDLE: LOGS("IDLE"); break;
        case STARTED: LOGS("STARTED"); break;
        case IN_PROGRESS: LOGS("IN_PROGRESS"); break;
        }
        LOGS(", ");
        LOGDEC(twi_dbg.buf[i].state_index & 0x3F);
        LOGS(", ");
        LOGHEX(twi_dbg.buf[i].crc);
    }
    for (uint8_t i = 0; i < TWI_BUFFER_SIZE; ++i) {
        LOGC(' ');
        LOGHEX(twi.buf[i]);
    }
    LOGC('\n');
    twi_dbg.index = 0;
}
#endif

void twi_init(uint8_t addr) {
    // SDA setup time
    // SDA hold time
    TWI0.CTRLA = TWI_SDASETUP_8CYC_gc | TWI_SDAHOLD_500NS_gc;
    // Address
    TWI0.SADDR = addr << TWI_ADDRMASK_gp;
    // Enable TWI client
    TWI0.SCTRLA = TWI_DIEN_bm | TWI_APIEN_bm | TWI_PIEN_bm | TWI_ENABLE_bm;
}

void twi_reset(void) {
    twi.cmd = TWI_CMD_NONE;
    twi.task = TWI_CMD_NONE;
}

uint8_t twi_get_task(void) {
    LOCKI();
    uint8_t task = twi.task;
    twi.task = TWI_CMD_NONE;
    UNLOCKI();
    return task;
}

bool twi_task_pending(void) {
    return twi.task != TWI_CMD_NONE;
}

bool twi_busy(void) {
    bool busy = twi.busy;
    twi.busy = false;
    return busy;
}

static bool twi_prepare_load(uint8_t count) {
    cli();
    set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_enable();
    while (twi.state != IDLE) {
        sei();
        sleep_cpu();
        cli();
    }
    sleep_disable();
    if (twi.task != TWI_CMD_NONE) {
        return false;
    }
    twi.count = count;
    twi.loaded = true;
    return true;
}

void twi_write(uint8_t count, const uint8_t *data) {
    if (twi_prepare_load(count)) {
        memcpy(twi.buf, data, count);
    }
    sei();
}

void twi_write_P(uint8_t count, const __flash uint8_t *data) {
    if (twi_prepare_load(count)) {
        memcpy_P(twi.buf, data, count);
    }
    sei();
}

void twi_read(struct twi_data *data) {
    data->task = TWI_CMD_NONE;
    data->count = 0;
    cli();
    if (twi.task != TWI_CMD_NONE && twi.state != IN_PROGRESS) {
        data->task = twi.task;
        memcpy(data->buf, twi.buf, twi.count);
        data->count = twi.count;
        twi.task = TWI_CMD_NONE;
        twi.blocked = false;
    }
    sei();
}
