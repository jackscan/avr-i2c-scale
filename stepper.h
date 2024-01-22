/*
    SPDX-FileCopyrightText: 2023 Mathias Fiedler
    SPDX-License-Identifier: MIT
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

void stepper_init(void);
/**
 * @brief Start stepper motor.
 *
 * @param dir Direction of rotation
 * @param cycles Number of full steps minus one
 * @param ramp8ms maximum speed to ramp up to
 */
void stepper_rotate(bool dir, uint8_t cycles, uint8_t maxspd);

/**
 * @brief Stop any running rotation.
 */
void stepper_stop(void);
