#ifndef __RNG_H
#define __RNG_H

/**
 * @file rng.h
 * @addtogroup BSP
 *
 * @brief  Cross-platform declaration "RNG" bsp module.
 *
 * @author Alexandre Abadie <alexandre.abadie@inria.fr>
 *
 * @copyright Inria, 2023
 */

#include <stdint.h>

//=========================== defines ==========================================

//=========================== prototypes =======================================

/**
 * @brief Configure the random number generator (RNG)
 */
void db_rng_init(void);

/**
 * @brief Read a random value (8 bits)
 *
 * @param[out] value address of the output value
 */
void db_rng_read(uint8_t *value);

#endif
