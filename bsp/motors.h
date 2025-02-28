#ifndef __MOTORS_H
#define __MOTORS_H

/**
 * @file motors.h
 * @addtogroup BSP
 *
 * @brief  Cross-platform declaration "motors" bsp module.
 *
 * @author Said Alvarado-Marin <said-alexander.alvarado-marin@inria.fr>
 *
 * @copyright Inria, 2022
 */

#include <nrf.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Configures the PMW0 peripheral to work with the onboard DotBot RGB Motor driver
 *
 * The DotBot uses a DRV8833 dual H-bridge driver with a 4 pmw control interface.
 * the PWM0 peripheral is used to generate the pwm signals it requires.
 *
 * PWM frequency = 10Khz
 * PWM resolution = 100 units (1us resolution)
 *
 */
void db_motors_init(void);

/**
 * @brief Set the percentage speed of the right and left motors on the DotBot
 *
 *  Each motor input variable receives a percentage speed from -100 to 100.
 *  Positive values turn the motor forward.
 *  Negative values turn the motor backward.
 *  Zero, stops the motor
 *
 * @param[in] l_speed speed of the left motor [-100, 100]
 * @param[in] r_speed speed of the left motor [-100, 100]
 */
void db_motors_set_speed(int16_t l_speed, int16_t r_speed);

#endif
