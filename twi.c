/*
    SPDX-FileCopyrightText: 2023 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#include "twi.h"

#include "config.h"
#include "debug.h"
#include "nvm.h"
#include "util.h"
#include "version.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <string.h>

#define ACK  (TWI_ACKACT_ACK_gc | TWI_SCMD_RESPONSE_gc)
#define NACK (TWI_ACKACT_NACK_gc | TWI_SCMD_COMPTRANS_gc)
#define DONE TWI_SCMD_COMPTRANS_gc

struct {
    uint8_t cmd;
    uint8_t task;
    uint8_t result;
    uint8_t index;
    uint8_t count;
    uint8_t crc;
    uint8_t buf[TWI_BUFFER_SIZE];
    enum {
        IDLE,
        STARTED,
        IN_PROGRESS,
    } mode;
} twi = {.cmd = TWI_CMD_NONE, .task = TWI_CMD_NONE, .result = TWI_CMD_NONE};

#define DBG_SIZE 16

struct {
    uint8_t index;
    struct {
        uint8_t status;
        uint8_t mode_index;
        uint8_t result;
        uint8_t crc;
    } buf[DBG_SIZE];
} twi_dbg;

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
    case TWI_CMD_MEASURE_WEIGHT: // fallthrough
    case TWI_CMD_CALIB_WRITE:    // fallthrough
    case TWI_CMD_SET_ADDR:       // fallthrough
    case TWI_CMD_ADDR_WRITE:     // fallthrough
    case TWI_CMD_DISABLE_WD: twi.count = 1; break;
    default: twi.count = 0; break;
    }
}

static void finish_recv(void) {
    TWI0.SCTRLB = NACK;
    twi.mode = IDLE;

    switch (twi.cmd) {
    case TWI_CMD_GET_VERSION:
        twi.task = TWI_CMD_NONE;
        twi_write_P(TWI_CMD_GET_VERSION, sizeof(version_info),
                    (const __flash uint8_t *)&version_info);
        break;
    default:
        twi.task = twi.cmd;
        twi.result = TWI_CMD_NONE;
        break;
    }
}

ISR(TWI0_TWIS_vect) {
    uint8_t status = TWI0.SSTATUS;
    if ((status & TWI_APIF_bm) != 0) {
        if ((status & TWI_AP_bm) == TWI_AP_ADR_gc) {
            // Address
            if ((status & TWI_DIR_bm) == 0 || twi.result == twi.cmd) {
                TWI0.SCTRLB = ACK;
                twi.mode = STARTED;
                twi.index = 0;
                twi.crc = 0;
            } else {
                TWI0.SCTRLB = NACK;
                twi.mode = IDLE;
            }
        } else {
            // Stop
            TWI0.SCTRLB = DONE;
            twi.mode = IDLE;
        }
    } else if ((status & TWI_DIF_bm) != 0) {
        if ((status & TWI_DIR_bm) != 0) {
            // Send byte
            if (((status & TWI_RXACK_bm) != 0 && twi.mode != STARTED) ||
                twi.index > twi.count) {
                TWI0.SSTATUS = TWI_DIF_bm;
                TWI0.SCTRLB = DONE;
                twi.mode = IDLE;
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
                twi.mode = IN_PROGRESS;
            }
        } else if (twi.mode == STARTED) {
            // Recv first byte
            twi.cmd = TWI0.SDATA;
            prepare_recv();
            if (twi.count > 0) {
                TWI0.SCTRLB = ACK;
                twi.task = TWI_CMD_NONE;
                twi.mode = IN_PROGRESS;
            } else {
                // command without data
                finish_recv();
                // TWI0.SCTRLB = NACK;
                // twi.mode = IDLE;
                // twi.task = twi.cmd;
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
                // TWI0.SCTRLB = NACK;
                // twi.mode = IDLE;
                // twi.task = twi.cmd;
            }
        } else {
            // should never happen
            TWI0.SCTRLB = NACK;
            twi.mode = IDLE;
        }
    } else {
        TWI0.SCTRLB = NACK;
        twi.mode = IDLE;
    }

    if (twi_dbg.index < DBG_SIZE) {
        twi_dbg.buf[twi_dbg.index].status = status;
        twi_dbg.buf[twi_dbg.index].mode_index = (twi.mode << 6) | twi.index;
        twi_dbg.buf[twi_dbg.index].result = twi.result;
        twi_dbg.buf[twi_dbg.index].crc = twi.crc;
        ++twi_dbg.index;
    }
}

void twi_dump_dbg(void) {
    LOGS("TWI:\n");
    for (uint8_t i = 0; i < twi_dbg.index; i++) {
        LOG("%#x, ", twi_dbg.buf[i].status);
        switch (twi_dbg.buf[i].mode_index >> 6) {
        case IDLE: LOGS("IDLE"); break;
        case STARTED: LOGS("STARTED"); break;
        case IN_PROGRESS: LOGS("IN_PROGRESS"); break;
        }
        LOG(", %d, %x, %x\n", twi_dbg.buf[i].mode_index & 0x3F,
            twi_dbg.buf[i].result, twi_dbg.buf[i].crc);
    }
    for (uint8_t i = 0; i < TWI_BUFFER_SIZE; ++i) {
        LOG(" %#x", twi.buf[i]);
    }
    LOGC('\n');
    twi_dbg.index = 0;
}

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
    // twi.quality = 0;
    // twi.temp = TWI_TEMP_INVALID;
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

void twi_write_done(uint8_t result) {
    cli();
    if (twi.mode != IN_PROGRESS && twi.cmd == result) {
        twi.result = result;
        twi.count = 0;
    }
    sei();
}

void twi_write(uint8_t result, uint8_t count, const uint8_t *data) {
    cli();
    if (twi.mode != IN_PROGRESS && twi.cmd == result) {
        memcpy(twi.buf, data, count);
        twi.result = result;
        twi.count = count;
    }
    sei();
}

void twi_write_P(uint8_t result, uint8_t count, const __flash uint8_t *data) {
    cli();
    if (twi.mode != IN_PROGRESS && twi.cmd == result) {
        memcpy_P(twi.buf, data, count);
        twi.result = result;
        twi.count = count;
    }
    sei();
}

void twi_read(struct twi_data *data) {
    data->task = TWI_CMD_NONE;
    data->count = 0;
    cli();
    if (twi.task != TWI_CMD_NONE && twi.mode != IN_PROGRESS) {
        data->task = twi.task;
        memcpy(data->buf, twi.buf, twi.count);
        data->count = twi.count;
        twi.task = TWI_CMD_NONE;
    }
    sei();
}

// void twi_set_temp(int8_t temp) {
//     LOCKI();
//     twi.temp = temp;
//     UNLOCKI();
// }

// void twi_set_weight(uint32_t weight, uint8_t count) {
//     LOCKI();
//     twi.weight = weight;
//     twi.count = count;
//     UNLOCKI();
// }
