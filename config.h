/*
    SPDX-FileCopyrightText: 2022 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#pragma once

#define VALVE_PORT PORTA
#define VALVE_BIT  (1 << 6)

#define TXD_PORT PORTB
#define TXD_BIT (1 << 2)

#define RXD_PORT PORTB
#define RXD_BIT (1 << 3)
#define RXD_PINCTRL (RXD_PORT.PIN3CTRL)

#define BAUDRATE 57600UL

#define MISO_PORT PORTA
#define MISO_BIT  (1 << 2)
#define MISO_PINCTRL (MISO_PORT.PIN2CTRL)
#define MISO_PORT_VECT  PORTA_PORT_vect

#define SCK_PORT PORTA
#define SCK_BIT  (1 << 3)

#define STP_NSLP_PORT PORTA
#define STP_NSLP_BIT  (1 << 7)
#define STP_DIR_PORT  PORTA
#define STP_DIR_BIT   (1 << 4)
#define STP_STEP_PORT PORTA
#define STP_STEP_BIT  (1 << 5)

// #define UPDI     PA0
// #define SDA      PB1
// #define SCL      PB0
// #define MOSI     PA1
// #define MISO     PA2
// #define SCK      PA3
// #define LED      PA5
// #define VALVE    PA6
// #define RXD      PB3
// #define TXD      PB2
// #define STP_NSLP PA7
// #define STP_DIR  PA4
// #define STP_STEP PA5
// #define STP1  PA1
// #define STP2  PA4
// #define STP3  PA5
// #define STP4  PA7
