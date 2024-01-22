/*
    SPDX-FileCopyrightText: 2023 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

void stepper_init(bool dir);
/**
 * @brief Start stepper motor.
 *
 * @param cycles Number of full steps
 * @param ramp8ms maximum speed to ramp up to
 */
void stepper_rotate(uint8_t cycles, uint8_t maxspd);

/**
 * @brief Stop any running rotation.
 */
void stepper_stop(void);
