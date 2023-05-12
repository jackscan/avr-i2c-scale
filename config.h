/*
    SPDX-FileCopyrightText: 2022 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#pragma once

#define VALVE_PORT PORTA
#define VALVE_BIT  (1 << 6)

#define LED_PORT PORTA
#define LED_BIT (1 << 4)

#define LED2_PORT PORTA
#define LED2_BIT (1 << 5)

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

// #define UPDI  PA0
// #define SDA   PB1
// #define SCL   PB0
// #define MOSI  PA1
// #define MISO  PA2
// #define SCK   PA3
// #define LED   PA5
// #define VALVE PA6
// #define RXD   PB3
// #define TXD   PB2
