/**
 * @file 01bsp_lighthouse.c
 * @author Said Alvarado-Marin <said-alexander.alvarado-marin@inria.fr>
 * @brief This is a short example of how to interface with the lighthouse v2 chip in the DotBot board.
 *
 * Load this program on your board. LED should blink blue when it receives a valid lighthouse 2 signal.
 *
 * @date 2022
 *
 * @copyright Inria, 2022
 *
 */
#include <nrf.h>
#include <stdio.h>
#include <stdlib.h>
#include "board.h"
#include "lh2.h"

//=========================== defines ==========================================

#define DB2_LH2_FULL_COMPUTATION 1

//=========================== variables ========================================

static db_lh2_t _lh2;

#if defined(NRF5340_XXAA) && defined(NRF_APPLICATION)
///! LH2 event gpio
static const gpio_t _lh2_e_gpio = {
    .port = 0,
    .pin  = 26,
};

///! LH2 data gpio
static const gpio_t _lh2_d_gpio = {
    .port = 0,
    .pin  = 25,
};
#else
///! LH2 event gpio
static const gpio_t _lh2_e_gpio = {
    .port = 0,
    .pin  = 30,
};

///! LH2 data gpio
static const gpio_t _lh2_d_gpio = {
    .port = 0,
    .pin  = 29,
};
#endif

//=========================== main =============================================

/**
 *  @brief The program starts executing here.
 */
int main(void) {
    // Initialize the board core features (voltage regulator)
    db_board_init();

    // Initialize the LH2
    db_lh2_init(&_lh2, &_lh2_d_gpio, &_lh2_e_gpio);
    db_lh2_start(&_lh2);

    while (1) {
        // wait until something happens e.g. an SPI interrupt
        __WFE();
        db_lh2_process_raw_data(&_lh2);

        if (_lh2.state == DB_LH2_RAW_DATA_READY) {
            // Stop the LH2 internal engine before doing other computations/sending radio packets, etc
            db_lh2_stop(&_lh2);

            if (DB2_LH2_FULL_COMPUTATION) {
                // the location function has to be running all the time
                db_lh2_process_location(&_lh2);
            }

            db_lh2_start(&_lh2);
        }
    }

    // one last instruction, doesn't do anything, it's just to have a place to put a breakpoint.
    __NOP();
}
