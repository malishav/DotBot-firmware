/**
 * @file 01bsp_rng.c
 * @author Alexandre Abadie <alexandre.abadie@inria.fr>
 * @brief This is a short example of how to use the RNG api.
 *
 * @copyright Inria, 2022
 *
 */
#include <nrf.h>
#include <stdint.h>
#include <stdio.h>
#include "rng.h"
#include "timer_hf.h"

//=========================== main =============================================

/**
 *  @brief The program starts executing here.
 */
int main(void) {
    db_timer_hf_init();
    db_rng_init();

    while (1) {
        uint8_t value;
        db_rng_read(&value);
        printf("Random value: %d\n", value);

        db_timer_hf_delay_ms(100);
    }

    // one last instruction, doesn't do anything, it's just to have a place to put a breakpoint.
    __NOP();
}
