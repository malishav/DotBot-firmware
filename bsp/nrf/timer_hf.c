/**
 * @file timer_hf.c
 * @addtogroup BSP
 *
 * @brief  nRF52833-specific definition of the "timer hf" bsp module.
 *
 * @author Alexandre Abadie <alexandre.abadie@inria.fr>
 *
 * @copyright Inria, 2022
 */
#include <nrf.h>
#include <nrf_peripherals.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "clock.h"
#include "timer_hf.h"

//=========================== define ===========================================

#if defined(NRF5340_XXAA)
#if defined(NRF_APPLICATION)
#define TIMER_HF (NRF_TIMER2_S)  ///< Backend TIMER peripheral used by the timer
#elif defined(NRF_NETWORK)
#define TIMER_HF (NRF_TIMER2_NS)  ///< Backend TIMER peripheral used by the timer
#endif
#define TIMER_HF_IRQ      (TIMER2_IRQn)        ///< IRQ corresponding to the TIMER used
#define TIMER_HF_ISR      (TIMER2_IRQHandler)  ///< ISR function handler corresponding to the TIMER used
#define TIMER_HF_CB_CHANS (TIMER2_CC_NUM - 1)  ///< Number of channels that can be used for periodic callbacks
#else
#define TIMER_HF          (NRF_TIMER4)         ///< Backend TIMER peripheral used by the timer
#define TIMER_HF_IRQ      (TIMER4_IRQn)        ///< IRQ corresponding to the TIMER used
#define TIMER_HF_ISR      (TIMER4_IRQHandler)  ///< ISR function handler corresponding to the TIMER used
#define TIMER_HF_CB_CHANS (TIMER4_CC_NUM - 1)  ///< Number of channels that can be used for periodic callbacks
#endif

typedef struct {
    uint32_t      period_us;  ///< Period in ticks between each callback
    bool          one_shot;   ///< Whether this is a one shot callback
    timer_hf_cb_t callback;   ///< Pointer to the callback function
} timer_hf_callback_t;

typedef struct {
    timer_hf_callback_t timer_callback[TIMER_HF_CB_CHANS];  ///< List of timer callback structs
    bool                running;                            ///< Whether the delay timer is running
} timer_hf_vars_t;

//=========================== variables ========================================

static timer_hf_vars_t _timer_hf_vars;

//=========================== public ===========================================

void db_timer_hf_init(void) {
    // No delay is running after initialization
    _timer_hf_vars.running = false;

    // Configure and start High Frequency clock
    db_hfclk_init();

    // Configure the timer
    TIMER_HF->TASKS_CLEAR = 1;
    TIMER_HF->PRESCALER   = 4;  // Run TIMER at 1MHz
    TIMER_HF->BITMODE     = (TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos);
    TIMER_HF->INTENSET    = (1 << (TIMER_INTENSET_COMPARE0_Pos + TIMER_HF_CB_CHANS));
    NVIC_EnableIRQ(TIMER_HF_IRQ);

    // Start the timer
    TIMER_HF->TASKS_START = 1;
}

uint32_t db_timer_hf_now(void) {
    TIMER_HF->TASKS_CAPTURE[TIMER_HF_CB_CHANS] = 1;
    return TIMER_HF->CC[TIMER_HF_CB_CHANS];
}

void db_timer_hf_set_periodic_us(uint8_t channel, uint32_t us, timer_hf_cb_t cb) {
    assert(channel >= 0 && channel < TIMER_HF_CB_CHANS + 1);  // Make sure the required channel is correct
    assert(cb);                                               // Make sure the callback function is valid

    _timer_hf_vars.timer_callback[channel].period_us = us;
    _timer_hf_vars.timer_callback[channel].one_shot  = false;
    _timer_hf_vars.timer_callback[channel].callback  = cb;
    TIMER_HF->INTENSET                               = (1 << (TIMER_INTENSET_COMPARE0_Pos + channel));
    TIMER_HF->TASKS_CAPTURE[channel]                 = 1;
    TIMER_HF->CC[channel] += _timer_hf_vars.timer_callback[channel].period_us;
}

void db_timer_hf_set_oneshot_us(uint8_t channel, uint32_t us, timer_hf_cb_t cb) {
    assert(channel >= 0 && channel < TIMER_HF_CB_CHANS + 1);  // Make sure the required channel is correct
    assert(cb);                                               // Make sure the callback function is valid

    _timer_hf_vars.timer_callback[channel].period_us = us;
    _timer_hf_vars.timer_callback[channel].one_shot  = true;
    _timer_hf_vars.timer_callback[channel].callback  = cb;
    TIMER_HF->INTENSET                               = (1 << (TIMER_INTENSET_COMPARE0_Pos + channel));
    TIMER_HF->TASKS_CAPTURE[channel]                 = 1;
    TIMER_HF->CC[channel] += _timer_hf_vars.timer_callback[channel].period_us;
}

void db_timer_hf_set_oneshot_ms(uint8_t channel, uint32_t ms, timer_hf_cb_t cb) {
    db_timer_hf_set_oneshot_us(channel, ms * 1000UL, cb);
}

void db_timer_hf_set_oneshot_s(uint8_t channel, uint32_t s, timer_hf_cb_t cb) {
    db_timer_hf_set_oneshot_us(channel, s * 1000UL * 1000UL, cb);
}

void db_timer_hf_delay_us(uint32_t us) {
    TIMER_HF->TASKS_CAPTURE[TIMER_HF_CB_CHANS] = 1;
    TIMER_HF->CC[TIMER_HF_CB_CHANS] += us;
    _timer_hf_vars.running = true;
    while (_timer_hf_vars.running) {
        __WFE();
        __SEV();
        __WFE();
    }
}

void db_timer_hf_delay_ms(uint32_t ms) {
    db_timer_hf_delay_us(ms * 1000UL);
}

void db_timer_hf_delay_s(uint32_t s) {
    db_timer_hf_delay_us(s * 1000UL * 1000UL);
}

//=========================== interrupt ========================================

void TIMER_HF_ISR(void) {
    if (TIMER_HF->EVENTS_COMPARE[TIMER_HF_CB_CHANS] == 1) {
        TIMER_HF->EVENTS_COMPARE[TIMER_HF_CB_CHANS] = 0;
        _timer_hf_vars.running                      = false;
        __SEV();
    }

    for (uint8_t channel = 0; channel < TIMER_HF_CB_CHANS; ++channel) {
        if (TIMER_HF->EVENTS_COMPARE[channel] == 1) {
            TIMER_HF->EVENTS_COMPARE[channel] = 0;
            if (_timer_hf_vars.timer_callback[channel].one_shot) {
                TIMER_HF->INTENCLR = (1 << (TIMER_INTENCLR_COMPARE0_Pos + channel));
            } else {
                TIMER_HF->CC[channel] += _timer_hf_vars.timer_callback[channel].period_us;
            }
            if (_timer_hf_vars.timer_callback[channel].callback) {
                _timer_hf_vars.timer_callback[channel].callback();
            }
        }
    }
}
