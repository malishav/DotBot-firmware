#ifndef __RADIO_H
#define __RADIO_H

/**
 * @file radio.h
 * @addtogroup BSP
 *
 * @brief  Cross-platform declaration "radio" bsp module.
 *
 * @author Said Alvarado-Marin <said-alexander.alvarado-marin@inria.fr>
 *
 * @copyright Inria, 2022
 */
#include <stdint.h>
#include <nrf.h>

//=========================== defines ==========================================

#ifndef DEFAULT_NETWORK_ADDRESS
#define DEFAULT_NETWORK_ADDRESS 0x12345678UL  ///< Default network address
#endif

typedef enum {
    DB_RADIO_BLE_1MBit,
    DB_RADIO_BLE_2MBit,
    DB_RADIO_BLE_LR125Kbit,
    DB_RADIO_BLE_LR500Kbit,
} db_radio_ble_mode_t;

typedef void (*radio_cb_t)(uint8_t *packet, uint8_t length);  ///< Function pointer to the callback function called on packet receive

//=========================== public ===========================================

/**
 * @brief Initializes the RADIO peripheral
 *
 * After this function you must explicitly set the frequency of the radio
 * with the db_radio_set_frequency function.
 *
 * @param[in] callback pointer to a function that will be called each time a packet is received.
 * @param[in] mode     BLE mode used by the radio (1MBit, 2MBit, LR125KBit, LR500Kbit)
 *
 */
void db_radio_init(radio_cb_t callback, db_radio_ble_mode_t mode);

/**
 * @brief Set the tx-rx frequency of the radio, by the following formula
 *
 * Radio frequency 2400 + freq (MHz) [0, 100]
 *
 * @param[in] freq Frequency of the radio [0, 100]
 */
void db_radio_set_frequency(uint8_t freq);

/**
 * @brief Set the physical channel used of the radio
 *
 * Channels 37, 38 and 39 are BLE advertising channels.
 *
 * @param[in] channel BLE channel used by the radio [0-39]
 */
void db_radio_set_channel(uint8_t channel);

/**
 * @brief Set the network address used to send/receive radio packets
 *
 * @param[in] addr Network address
 */
void db_radio_set_network_address(uint32_t addr);

/**
 * @brief Sends a single packet through the Radio
 *
 * NOTE: Must configure the radio and the frequency before calling this function.
 * (with the functions db_radio_init db_radio_set_frequency).
 *
 * NOTE: The radio must not be receiving packets when calling this function.
 * (first call db_radio_rx_disable if needed)
 *
 * @param[in] packet pointer to the array of data to send over the radio (max size = 32)
 * @param[in] length Number of bytes to send (max size = 32)
 *
 */
void db_radio_tx(uint8_t *packet, uint8_t length);

/**
 * @brief Starts Receiving packets through the Radio
 *
 * NOTE: Must configure the radio and the frequency before calling this function.
 * (with the functions db_radio_init db_radio_set_frequency).
 *
 */
void db_radio_rx_enable(void);

/**
 * @brief Stops receiving packets through the Radio
 */
void db_radio_rx_disable(void);

#endif
