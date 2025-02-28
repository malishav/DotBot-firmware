/**
 * @file lh2.c
 * @addtogroup BSP
 *
 * @brief  nRF52833-specific definition of the "lh2" bsp module.
 *
 * @author Filip Maksimovic <filip.maksimovic@inria.fr>, Said Alvarado-Marin <said-alexander.alvarado-marin@inria.fr>
 *
 * @copyright Inria, 2022
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <nrf.h>

#include "gpio.h"
#include "lh2.h"
#include "timer_hf.h"

//=========================== defines =========================================

#define SPIM_INTERRUPT_PRIORITY                2           ///< Interrupt priority, as high as it will go
#define SPI_BUFFER_SIZE                        64          ///< Size of buffers used for SPI communications
#define SPI_FAKE_SCK_PIN                       6           ///< NOTE: SPIM needs an SCK pin to be defined, P1.6 is used because it's not an available pin in the BCM module.
#define SPI_FAKE_SCK_PORT                      1           ///< NOTE: SPIM needs an SCK pin to be defined, P1.6 is used because it's not an available pin in the BCM module.
#define FUZZY_CHIP                             0xFF        ///< not sure what this is about
#define LH2_LOCATION_ERROR_INDICATOR           0xFFFFFFFF  ///< indicate the location value is false
#define LH2_POLYNOMIAL_ERROR_INDICATOR         0xFF        ///< indicate the polynomial index is invalid
#define POLYNOMIAL_BIT_ERROR_INITIAL_THRESHOLD 4           ///< initial threshold of polynomial error
#define LH2_BUFFER_SIZE                        128         ///< buffer size containing lh2 frames
#define GPIOTE_CH_IN_ENV_HiToLo                1           ///< falling edge gpio channel
#define GPIOTE_CH_IN_ENV_LoToHi                2           ///< rising edge gpio channel
#define PPI_SPI_START_CHAN                     2
#define PPI_SPI_STOP_CHAN                      3

#if defined(NRF5340_XXAA) && defined(NRF_APPLICATION)
#define NRF_SPIM         NRF_SPIM4_S
#define SPIM_IRQ         SPIM4_IRQn
#define SPIM_IRQ_HANDLER SPIM4_IRQHandler
#define NRF_GPIOTE       NRF_GPIOTE0_S
#define NRF_PPI          NRF_DPPIC_S
#else
#define NRF_SPIM         NRF_SPIM3
#define SPIM_IRQ         SPIM3_IRQn
#define SPIM_IRQ_HANDLER SPIM3_IRQHandler
#endif

typedef struct {
    uint8_t buffer[LH2_BUFFER_SIZE];  ///< arrays of bits for local storage, contents of SPI transfer are copied into this
} lh2_buffer_t;

typedef struct {
    uint8_t      transfer_counter;                ///< counter of spi transfer in the current cycle
    uint8_t      spi_rx_buffer[SPI_BUFFER_SIZE];  ///< buffer where data coming from SPI are stored
    bool         buffers_ready;                   ///< specify when data buffers are ready to be process
    lh2_buffer_t data[LH2_LOCATIONS_COUNT];       ///< array containing demodulation data of each locations
    uint8_t      lha_packet_counter;              ///< number of packet received from LHA
    uint8_t      lhb_packet_counter;              ///< number of packet received from LHB
} lh2_vars_t;

//=========================== variables ========================================

static const uint32_t _polynomials[4] = {
    0x0001D258,
    0x00017E04,
    0x0001FF6B,
    0x00013F67,
};

static const uint32_t _end_buffers[4][16] = {
    {
        // p0
        0x00000000000000001,  // [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1] starting seed, little endian
        0b10101010110011101,  // 1/16 way through
        0b10001010101011010,  // 2/16 way through
        0b11001100100000010,  // 3/16 way through
        0b01100101100011111,  // 4/16 way through
        0b10010001101011110,  // 5/16 way through
        0b10100011001011111,  // 6/16 way through
        0b11110001010110001,  // 7/16 way through
        0b10111000110011011,  // 8/16 way through
        0b10100110100011110,  // 9/16 way through
        0b11001101100010000,  // 10/16 way through
        0b01000101110011111,  // 11/16 way through
        0b11100101011110101,  // 12/16 way through
        0b01001001110110111,  // 13/16 way through
        0b11011100110011101,  // 14/16 way through
        0b10000110101101011,  // 15/16 way through
    },
    {
        // p1
        0x00000000000000001,  // [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1] starting seed, little endian
        0b11010000110111110,  // 1/16 way through
        0b10110111100111100,  // 2/16 way through
        0b11000010101101111,  // 3/16 way through
        0b00101110001101110,  // 4/16 way through
        0b01000011000110100,  // 5/16 way through
        0b00010001010011110,  // 6/16 way through
        0b10100101111010001,  // 7/16 way through
        0b10011000000100001,  // 8/16 way through
        0b01110011011010110,  // 9/16 way through
        0b00100011101000011,  // 10/16 way through
        0b10111011010000101,  // 11/16 way through
        0b00110010100110110,  // 12/16 way through
        0b01000111111100110,  // 13/16 way through
        0b10001101000111011,  // 14/16 way through
        0b00111100110011100,  // 15/16 way through
    },
    {
        // p2
        0x00000000000000001,  // [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1] starting seed, little endian
        0b00011011011000100,  // 1/16 way through
        0b01011101010010110,  // 2/16 way through
        0b11001011001101010,  // 3/16 way through
        0b01110001111011010,  // 4/16 way through
        0b10110110011111010,  // 5/16 way through
        0b10110001110000001,  // 6/16 way through
        0b10001001011101001,  // 7/16 way through
        0b00000010011101011,  // 8/16 way through
        0b01100010101111011,  // 9/16 way through
        0b00111000001101111,  // 10/16 way through
        0b10101011100111000,  // 11/16 way through
        0b01111110101111111,  // 12/16 way through
        0b01000011110101010,  // 13/16 way through
        0b01001011100000011,  // 14/16 way through
        0b00010110111101110,  // 15/16 way through
    },
    {
        // p3
        0x00000000000000001,  // [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1] starting seed, little endian
        0b11011011110010110,  // 1/16 way through
        0b11000100000001101,  // 2/16 way through
        0b11100011000010110,  // 3/16 way through
        0b00011111010001100,  // 4/16 way through
        0b11000001011110011,  // 5/16 way through
        0b10011101110001010,  // 6/16 way through
        0b00001011001111000,  // 7/16 way through
        0b00111100010000101,  // 8/16 way through
        0b01001111001010100,  // 9/16 way through
        0b01011010010110011,  // 10/16 way through
        0b11111101010001100,  // 11/16 way through
        0b00110101011011111,  // 12/16 way through
        0b01110110010101011,  // 13/16 way through
        0b00010000110100010,  // 14/16 way through
        0b00010111110101110,  // 15/16 way through
    },
};

///! NOTE: SPIM needs an SCK pin to be defined, P1.6 is used because it's not an available pin in the BCM module
static const gpio_t _lh2_spi_fake_sck_gpio = {
    .port = 1,
    .pin  = 6,
};

static lh2_vars_t _lh2_vars;  ///< local data of the LH2 driver

//=========================== prototypes =======================================

// these functions are called in the order written to perform the LH2 localization
/**
 * @brief wiggle the data and envelope lines in a magical way to configure the TS4231 to continuously read for LH2 sweep signals.
 *
 * @param[in]   gpio_d  pointer to gpio data
 * @param[in]   gpio_e  pointer to gpio event
 */
void _initialize_ts4231(const gpio_t *gpio_d, const gpio_t *gpio_e);

/**
 * @brief
 * @param sample_buffer: SPI samples loaded into a local buffer
 * @return chipsH: 64-bits of demodulated data
 */
uint64_t _demodulate_light(uint8_t *sample_buffer);

/**
 * @brief from a 17-bit sequence and a polynomial, generate up to 64-17=47 bits as if the LFSR specified by poly were run forwards for numbits cycles, all little endian
 *
 * @param poly: 17-bit polynomial
 * @param bits_in: starting seed
 * @param numbits: number of bits
 *
 * @return sequence of bits resulting from running the LFSR forward
 */
uint64_t _poly_check(uint32_t poly, uint32_t bits, uint8_t numbits);

/**
 * @brief find out which LFSR polynomial the bit sequence is a member of
 *
 * @param[in] chipsH1: input sequences of bits from demodulation
 * @param[in] start_val: number of bits between the envelope falling edge and the beginning of the sequence where valid data has been found
 *
 * @return polynomial, indicating which polynomial was found, or FF for error (polynomial not found).
 */
uint8_t _determine_polynomial(uint64_t chipsH1, int8_t *start_val);

/**
 * @brief counts the number of 1s in a 64-bit
 *
 * @param bits_in, arbitrary bits
 *
 * @return cumulative number of 1s inside of bits_in
 */
uint64_t _hamming_weight(uint64_t bits_in);

/**
 * @brief finds the position of a 17-bit sequence (bits) in the sequence generated by polynomial3 with initial seed 1
 *
 * @param bits: 17-bit sequence
 *
 * @return count: location of the sequence
 */
uint32_t _reverse_count_p(uint8_t index, uint32_t bits);

/**
 * @brief Set a gpio as an INPUT with no pull-up or pull-down
 * @param[in] gpio: pin to configure as input [0-31]
 */
void _lh2_pin_set_input(const gpio_t *gpio);

/**
 * @brief Set a gpio as an OUTPUT with standard drive
 * @param[in] gpio: gpio to configure as input [0-31]
 */
void _lh2_pin_set_output(const gpio_t *gpio);

/**
 * @brief set-up GPIOTE so that events are configured for falling (GPIOTE_CH_IN) and rising edges (GPIOTE_CH_IN_ENV_HiToLo) of the envelope signal
 *
 * @param[in]   gpio_e  pointer to gpio event
 */
void _gpiote_setup(const gpio_t *gpio_e);

/**
 * @brief start SPI3 at falling edge of envelope, start timer2 at falling edge of envelope, stop/capture timer2 at rising edge of envelope
 */
void _ppi_setup(void);

/**
 * @brief spi3 setup
 *
 * @param[in]   gpio_d  pointer to gpio data
 */
void _spi_setup(const gpio_t *gpio_d);

//=========================== public ===========================================

void db_lh2_init(db_lh2_t *lh2, const gpio_t *gpio_d, const gpio_t *gpio_e) {
    // Initialize the TS4231 on power-up - this is only necessary when power-cycling
    _initialize_ts4231(gpio_d, gpio_e);

    // Configure the necessary Pins in the GPIO peripheral  (MOSI and CS not needed)
    _lh2_pin_set_input(gpio_d);                    // Data_pin will become the MISO pin
    _lh2_pin_set_output(&_lh2_spi_fake_sck_gpio);  // set SCK as Output.

    _spi_setup(gpio_d);

    // Setup the LH2 local variables
    memset(_lh2_vars.spi_rx_buffer, 0, SPI_BUFFER_SIZE);
    _lh2_vars.transfer_counter   = 0;
    _lh2_vars.lha_packet_counter = 0;
    _lh2_vars.lhb_packet_counter = 0;
    _lh2_vars.buffers_ready      = false;

    // Setup LH2 data
    for (uint8_t location = 0; location < LH2_LOCATIONS_COUNT; location++) {
        lh2->raw_data[location].bits_sweep           = 0;
        lh2->raw_data[location].selected_polynomial  = LH2_POLYNOMIAL_ERROR_INDICATOR;
        lh2->raw_data[location].bit_offset           = 0;
        lh2->locations[location].selected_polynomial = LH2_POLYNOMIAL_ERROR_INDICATOR;
        lh2->locations[location].lfsr_location       = LH2_LOCATION_ERROR_INDICATOR;
        //_lh2_vars.data[location].envelope_duration   = 0xFFFFFFFF;
        memset(_lh2_vars.data[location].buffer, 0, LH2_BUFFER_SIZE);
    }

    // initialize GPIOTEs
    _gpiote_setup(gpio_e);

    // initialize PPI
    _ppi_setup();

    lh2->state = DB_LH2_IDLE;
}

void db_lh2_start(db_lh2_t *lh2) {
    db_lh2_reset(lh2);
    NRF_PPI->CHENSET = (1 << PPI_SPI_START_CHAN) | (1 << PPI_SPI_STOP_CHAN);

    lh2->state = DB_LH2_RUNNING;
}

void db_lh2_stop(db_lh2_t *lh2) {
    NRF_PPI->CHENCLR = (1 << PPI_SPI_START_CHAN) | (1 << PPI_SPI_STOP_CHAN);
    lh2->state       = DB_LH2_IDLE;
}

void db_lh2_reset(db_lh2_t *lh2) {
    _lh2_vars.transfer_counter   = 0;
    _lh2_vars.lha_packet_counter = 0;
    _lh2_vars.lhb_packet_counter = 0;
    _lh2_vars.buffers_ready      = false;
    lh2->state                   = DB_LH2_RUNNING;
}

void db_lh2_process_raw_data(db_lh2_t *lh2) {
    if (!_lh2_vars.buffers_ready) {
        return;
    }

    for (uint8_t location = 0; location < LH2_LOCATIONS_COUNT; location++) {
        lh2->raw_data[location].bits_sweep = 0;
        // perform the demodulation + poly search on the received packets
        // convert the SPI reading to bits via zero-crossing counter demodulation and differential/biphasic manchester decoding
        lh2->raw_data[location].bits_sweep = _demodulate_light(_lh2_vars.data[location].buffer);
        // figure out which polynomial each one of the two samples come from.
        lh2->raw_data[location].selected_polynomial = _determine_polynomial(lh2->raw_data[location].bits_sweep, &lh2->raw_data[location].bit_offset);
    }

    if ((lh2->raw_data[0].selected_polynomial == LH2_POLYNOMIAL_ERROR_INDICATOR) ||
        (lh2->raw_data[1].selected_polynomial == LH2_POLYNOMIAL_ERROR_INDICATOR)) {  // failure to find one of the two polynomials - start from scratch and grab another capture
        db_lh2_reset(lh2);
        return;
    }

    lh2->state = DB_LH2_RAW_DATA_READY;
}

void db_lh2_process_location(db_lh2_t *lh2) {
    if (lh2->state != DB_LH2_RAW_DATA_READY) {
        return;
    }

    // compute LFSR locations and detect invalid packets
    for (uint8_t location = 0; location < LH2_LOCATIONS_COUNT; location++) {
        lh2->locations[location].selected_polynomial = lh2->raw_data[location].selected_polynomial;
        // find location of the first data set by counting the LFSR backwards
        lh2->locations[location].lfsr_location = _reverse_count_p(
                                                     lh2->raw_data[location].selected_polynomial,
                                                     lh2->raw_data[location].bits_sweep >> (47 - lh2->raw_data[location].bit_offset)) -
                                                 lh2->raw_data[location].bit_offset;
    }

    uint32_t tmp_locations[LH2_LOCATIONS_COUNT] = { 0 };
    for (uint8_t location = 0; location < LH2_LOCATIONS_COUNT; location++) {
        tmp_locations[location] = lh2->locations[location].lfsr_location;
    }

    // results from 2 LH2 were discovered - sort the two pairs
    if (lh2->locations[0].lfsr_location > lh2->locations[1].lfsr_location) {
        lh2->locations[0].selected_polynomial = lh2->raw_data[1].selected_polynomial;
        lh2->locations[0].lfsr_location       = tmp_locations[1];
        lh2->locations[1].selected_polynomial = lh2->raw_data[0].selected_polynomial;
        lh2->locations[1].lfsr_location       = tmp_locations[0];
    }

    for (uint8_t location = 0; location < LH2_LOCATIONS_COUNT; location++) {
        memset(_lh2_vars.data[location].buffer, 0, LH2_BUFFER_SIZE);
    }
    lh2->state = DB_LH2_LOCATION_READY;
}

//=========================== private ==========================================

void _initialize_ts4231(const gpio_t *gpio_d, const gpio_t *gpio_e) {

    // Configure the wait timer
    db_timer_hf_init();

    // Filip's code define these pins as inputs, and then changes them quickly to outputs. Not sure why, but it works.
    _lh2_pin_set_input(gpio_d);
    _lh2_pin_set_input(gpio_e);

    // start the TS4231 initialization
    // Wiggle the Envelope and Data pins
    _lh2_pin_set_output(gpio_e);
    db_timer_hf_delay_us(10);
    nrf_port[gpio_e->port]->OUTSET = 1 << gpio_e->pin;  // set pin HIGH
    db_timer_hf_delay_us(10);
    nrf_port[gpio_e->port]->OUTCLR = 1 << gpio_e->pin;  // set pin LOW
    db_timer_hf_delay_us(10);
    nrf_port[gpio_e->port]->OUTSET = 1 << gpio_e->pin;
    db_timer_hf_delay_us(10);
    _lh2_pin_set_output(gpio_d);
    db_timer_hf_delay_us(10);
    nrf_port[gpio_d->port]->OUTSET = 1 << gpio_d->pin;
    db_timer_hf_delay_us(10);
    // Turn the pins back to inputs
    _lh2_pin_set_input(gpio_d);
    _lh2_pin_set_input(gpio_e);
    // finally, wait 1 milisecond
    db_timer_hf_delay_us(1000);

    // Send the configuration magic number/sequence
    uint16_t config_val = 0x392B;
    // Turn the Data and Envelope lines back to outputs and clear them.
    _lh2_pin_set_output(gpio_e);
    _lh2_pin_set_output(gpio_d);
    db_timer_hf_delay_us(10);
    nrf_port[gpio_d->port]->OUTCLR = 1 << gpio_d->pin;
    db_timer_hf_delay_us(10);
    nrf_port[gpio_e->port]->OUTCLR = 1 << gpio_e->pin;
    db_timer_hf_delay_us(10);
    // Send the magic configuration value, MSB first.
    for (uint8_t i = 0; i < 15; i++) {

        config_val = config_val << 1;
        if ((config_val & 0x8000) > 0) {
            nrf_port[gpio_d->port]->OUTSET = 1 << gpio_d->pin;
        } else {
            nrf_port[gpio_d->port]->OUTCLR = 1 << gpio_d->pin;
        }

        // Toggle the Envelope line as a clock.
        db_timer_hf_delay_us(10);
        nrf_port[gpio_e->port]->OUTSET = 1 << gpio_e->pin;
        db_timer_hf_delay_us(10);
        nrf_port[gpio_e->port]->OUTCLR = 1 << gpio_e->pin;
        db_timer_hf_delay_us(10);
    }
    // Finish send sequence and turn pins into inputs again.
    nrf_port[gpio_d->port]->OUTCLR = 1 << gpio_d->pin;
    db_timer_hf_delay_us(10);
    nrf_port[gpio_e->port]->OUTSET = 1 << gpio_e->pin;
    db_timer_hf_delay_us(10);
    nrf_port[gpio_d->port]->OUTSET = 1 << gpio_d->pin;
    db_timer_hf_delay_us(10);
    _lh2_pin_set_input(gpio_d);
    _lh2_pin_set_input(gpio_e);
    // Finish by waiting 10usec
    db_timer_hf_delay_us(10);

    // Now read back the sequence that the TS4231 answers.
    _lh2_pin_set_output(gpio_e);
    _lh2_pin_set_output(gpio_d);
    db_timer_hf_delay_us(10);
    nrf_port[gpio_d->port]->OUTCLR = 1 << gpio_d->pin;
    db_timer_hf_delay_us(10);
    nrf_port[gpio_e->port]->OUTCLR = 1 << gpio_e->pin;
    db_timer_hf_delay_us(10);
    nrf_port[gpio_d->port]->OUTSET = 1 << gpio_d->pin;
    db_timer_hf_delay_us(10);
    nrf_port[gpio_e->port]->OUTSET = 1 << gpio_e->pin;
    db_timer_hf_delay_us(10);
    // Set Data pin as an input, to receive the data
    _lh2_pin_set_input(gpio_d);
    db_timer_hf_delay_us(10);
    nrf_port[gpio_e->port]->OUTCLR = 1 << gpio_e->pin;
    db_timer_hf_delay_us(10);
    // Use the Envelope pin to output a clock while the data arrives.
    for (uint8_t i = 0; i < 14; i++) {
        nrf_port[gpio_e->port]->OUTSET = 1 << gpio_e->pin;
        db_timer_hf_delay_us(10);
        nrf_port[gpio_e->port]->OUTCLR = 1 << gpio_e->pin;
        db_timer_hf_delay_us(10);
    }

    // Finish the configuration procedure
    _lh2_pin_set_output(gpio_d);
    db_timer_hf_delay_us(10);
    nrf_port[gpio_e->port]->OUTSET = 1 << gpio_e->pin;
    db_timer_hf_delay_us(10);
    nrf_port[gpio_d->port]->OUTSET = 1 << gpio_d->pin;
    db_timer_hf_delay_us(10);

    nrf_port[gpio_e->port]->OUTCLR = 1 << gpio_e->pin;
    db_timer_hf_delay_us(10);
    nrf_port[gpio_d->port]->OUTCLR = 1 << gpio_d->pin;
    db_timer_hf_delay_us(10);
    nrf_port[gpio_e->port]->OUTSET = 1 << gpio_e->pin;
    db_timer_hf_delay_us(10);

    _lh2_pin_set_input(gpio_d);
    _lh2_pin_set_input(gpio_e);

    db_timer_hf_delay_us(50000);
}

uint64_t _demodulate_light(uint8_t *sample_buffer) {  // bad input variable name!!
    // TODO: rename sample_buffer
    // TODO: make it a void and have chips be a modified pointer thingie
    // FIXME: there is an edge case where I throw away an initial "1" and do not count it in the bit-shift offset, resulting in an incorrect error of 1 in the LFSR location
    uint8_t chip_index;
    uint8_t local_buffer[128];
    uint8_t zccs_1[128];
    uint8_t chips1[128];  // TODO: give this a better name.
    uint8_t temp_byte_N;  // TODO: bad variable name "temp byte"
    uint8_t temp_byte_M;  // TODO: bad variable name "temp byte"

    // initialize loop variables
    uint8_t  ii = 0x00;
    int      jj = 0;
    int      kk = 0;
    uint64_t gg = 0;

    // initialize temporary "ones counter" variable that counts consecutive ones
    int ones_counter = 0;

    // initialize result:
    uint64_t chipsH1 = 0;

    // FIND ZERO CROSSINGS
    chip_index         = 0;
    zccs_1[chip_index] = 0x01;

    memcpy(local_buffer, sample_buffer, 128);

    // for loop over bytes of the SPI buffer (jj), nested with a for loop over bits in each byte (ii)
    for (jj = 0; jj < 128; jj++) {
        // edge case - check if last bit (LSB) of previous byte is the same as first bit (MSB) of current byte
        // if it is not, increment chip_index and reset count
        if (jj != 0) {
            temp_byte_M = (local_buffer[jj - 1]) & (0x01);   // previous byte's LSB
            temp_byte_N = (local_buffer[jj] >> 7) & (0x01);  // current byte's MSB
            if (temp_byte_M != temp_byte_N) {
                chip_index++;
                zccs_1[chip_index] = 1;
            } else {
                zccs_1[chip_index] += 1;
            }
        }
        // look at one byte at a time
        for (ii = 7; ii > 0; ii--) {
            temp_byte_M = ((local_buffer[jj]) >> (ii)) & (0x01);      // bit shift by ii and mask
            temp_byte_N = ((local_buffer[jj]) >> (ii - 1)) & (0x01);  // bit shift by ii-1 and mask
            if (temp_byte_M == temp_byte_N) {
                zccs_1[chip_index] += 1;
            } else {
                chip_index++;
                zccs_1[chip_index] = 1;
            }
        }
    }

    // threshold the zero crossings into: likely one chip, likely two zero chips, or fuzzy
    for (jj = 0; jj < 128; jj++) {
        // not memory efficient, but ok for readability, turn ZCCS into chips by thresholding
        if (zccs_1[jj] >= 5) {
            chips1[jj] = 0;  // it's a very likely zero
        } else if (zccs_1[jj] <= 3) {
            chips1[jj] = 1;  // it's a very likely one
        } else {
            chips1[jj] = FUZZY_CHIP;  // fuzzy
        }
    }
    // final bit is bugged, make it fuzzy:
    // chips1[127] = 0xFF;

    // DEMODULATION:
    // basic principles, in descending order of importance:
    //  1) an odd number of ones in a row is not allowed - this must be avoided at all costs
    //  2) finding a solution to #1 given a set of data is quite cumbersome without certain assumptions
    //    a) a fuzzy before an odd run of 1s is almost always a 1
    //    b) a fuzzy between two even runs of 1s is almost always a 0
    //    c) a fuzzy after an even run of 1s is usually a a 0
    //  3) a detected 1 is rarely wrong, but detected 0s can be, this is especially common in low-SNR readings
    //    exception: if the first bit is a 1 it is NOT reliable because the capture is asynchronous
    //  4) this is not perfect, but the earlier the chip, the more likely that it is correct. Polynomials can be used to fix bit errors later in the reading
    // known bugs/issues:
    //  1) if there are many ones at the very beginning of the reading, the algorithm will mess it up
    //  2) in some instances, the count value will be off by approximately 5, the origin of this bug is unknown at the moment
    // DEMODULATE PACKET:

    // reset variables:
    kk           = 0;
    ones_counter = 0;
    jj           = 0;
    for (jj = 0; jj < 128;) {      // TODO: 128 is such an easy magic number to get rid of...
        gg = 0;                    // TODO: this is not used here?
        if (chips1[jj] == 0x00) {  // zero, keep going, reset state
            jj++;
            ones_counter = 0;
        }
        if (chips1[jj] == 0x01) {  // one, keep going, keep track of the # of ones
                                   // k_msleep(10);
            if (jj == 0) {         // edge case - first chip = 1 is unreliable, do not increment 1s counter
                jj++;
            } else {
                jj           = jj + 1;
                ones_counter = ones_counter + 1;
            }
        }

        if ((jj == 127) & (chips1[jj] == FUZZY_CHIP)) {
            chips1[jj] = 0x00;
        } else if ((chips1[jj] == FUZZY_CHIP) & (ones_counter == 0)) {  // fuzz after a zero
                                                                        // k_msleep(10);
            if (chips1[jj + 1] == 0) {                                  // zero then fuzz then zero -> fuzz is a zero
                jj++;
                chips1[jj - 1] = 0;
            } else if (chips1[jj + 1] == FUZZY_CHIP) {  // zero then fuzz then fuzz -> just move on, you're probably screwed
                // k_msleep(10);
                jj += 2;
            } else if (chips1[jj + 1] == 1) {  // zero then fuzz then one -> investigate
                kk           = 1;
                ones_counter = 0;
                while (chips1[jj + kk] == 1) {
                    ones_counter++;
                    kk++;
                }
                if (ones_counter % 2 == 1) {  // fuzz -> odd ones, the fuzz is a 1
                    jj++;
                    chips1[jj - 1] = 1;
                    ones_counter   = 1;
                } else if (ones_counter % 2 == 0) {  // fuzz -> even ones, move on for now, it's indeterminate
                    jj++;
                    ones_counter = 0;  // temporarily treat as a 0 for counting purposes
                } else {               // catch statement
                    jj++;
                }
            }
        } else if ((chips1[jj] == FUZZY_CHIP) & (ones_counter != 0)) {  // ones then fuzz
                                                                        // k_msleep(10);
            if ((ones_counter % 2 == 0) & (chips1[jj + 1] == 0)) {      // even ones then fuzz then zero, fuzz is a zero
                jj++;
                chips1[jj - 1] = 0;
                ones_counter   = 0;
            }
            if ((ones_counter % 2 == 0) & (chips1[jj + 1] != 0)) {  // even ones then fuzz then not zero - investigate
                if (chips1[jj + 1] == 1) {                          // subsequent bit is a 1
                    kk = 1;
                    while (chips1[jj + kk] == 1) {
                        ones_counter++;
                        kk++;
                    }
                    if (ones_counter % 2 == 1) {  // indicates an odd # of 1s, so the fuzzy has to be a 1
                        jj++;
                        chips1[jj - 1] = 1;
                        ones_counter   = 1;              // not actually 1, but it's ok for modulo purposes
                    } else if (ones_counter % 2 == 0) {  // even ones -> fuzz -> even ones, indeterminate
                        jj++;
                        ones_counter = 0;
                    }
                } else if (chips1[jj + 1] == FUZZY_CHIP) {  // subsequent bit is a fuzzy - skip for now...
                    jj++;
                }
            } else if ((ones_counter % 2 == 1) & (chips1[jj + 1] == FUZZY_CHIP)) {  // odd ones then fuzz then fuzz, fuzz is 1 then 0
                jj += 2;
                chips1[jj - 1] = 0;
                chips1[jj - 2] = 1;
                ones_counter   = 0;
            } else if ((ones_counter % 2 == 1) & (chips1[jj + 1] != 0)) {  // odd ones then fuzz then not zero - the fuzzy has to be a 1
                jj++;
                ones_counter++;
                chips1[jj - 1] = 1;
            } else {  // catch statement
                jj++;
            }
        }
    }
    // finish up demodulation, pick off straggling fuzzies and odd runs of 1s
    for (jj = 0; jj < 128;) {
        if (chips1[jj] == 0x00) {                   // zero, keep going, reset state
            if (ones_counter % 2 == 1) {            // implies an odd # of 1s
                chips1[jj - ones_counter - 1] = 1;  // change the bit before the run of 1s to a 1 to make it even
            }
            jj++;
            ones_counter = 0;
        } else if (chips1[jj] == 0x01) {  // one, keep going, keep track of the # of ones
            if (jj == 0) {                // edge case - first chip = 1 is unreliable, do not increment 1s counter
                jj++;
            } else {
                jj           = jj + 1;
                ones_counter = ones_counter + 1;
            }
        } else if (chips1[jj] == FUZZY_CHIP) {
            // if (ones_counter==0) { // fuzz after zeros, if the next chip is a 1, make it a 1, else make it a zero
            //     if (chips1[jj+1]==1) {
            //         jj+1;
            //         chips1[jj-1] = 1;
            //         ones_counter++;
            //     }
            //     else {
            //         jj++;
            //     }
            // }  <---- this is commented out because this is a VERY rare edge case and seems to be causing occasional problems w/ otherwise clean packets
            if ((ones_counter != 0) & (ones_counter % 2 == 0)) {  // fuzz after even ones - at this point this is almost always a 0
                jj++;
                chips1[jj - 1] = 0;
                ones_counter   = 0;
            } else if (ones_counter % 2 == 1) {  // fuzz after odd ones - exceedingly uncommon at this point, make it a 1
                jj++;
                chips1[jj - 1] = 1;
                ones_counter++;
            } else {  // catch statement
                jj++;
            }
        } else {  // catch statement
            jj++;
        }
    }

    // next step in demodulation: take the resulting array of 1 and 0 chips and put them into a single 64-bit unsigned int
    // this is primarily for easy manipulation for polynomial searching
    chip_index = 0;  // TODO: rename "chip index" it's not descriptive
    chipsH1    = 0;
    gg         = 0;    // looping/while break indicating variable, reset to 0
    while (gg < 64) {  // very last one - make all remaining fuzzies 0 and load it into two 64-bit longs
        if (chip_index > 127) {
            gg = 65;  // break
        }
        if ((chip_index == 0) & (chips1[chip_index] == 0x01)) {  // first bit is a 1 - ignore it
            chip_index = chip_index + 1;
        } else if ((chip_index == 0) & (chips1[chip_index] == FUZZY_CHIP)) {  // first bit is fuzzy - ignore it
            chip_index = chip_index + 1;
        } else if (gg == 63) {  // load the final bit
            if (chips1[chip_index] == 0) {
                chipsH1 &= 0xFFFFFFFFFFFFFFFE;
                gg         = gg + 1;
                chip_index = chip_index + 1;
            } else if (chips1[chip_index] == FUZZY_CHIP) {
                chipsH1 &= 0xFFFFFFFFFFFFFFFE;
                gg         = gg + 1;
                chip_index = chip_index + 1;
            } else if (chips1[chip_index] == 0x01) {
                chipsH1 |= 0x0000000000000001;
                gg         = gg + 1;
                chip_index = chip_index + 2;
            }
        } else {  // load the bit in!!
            if (chips1[chip_index] == 0) {
                chipsH1 &= 0xFFFFFFFFFFFFFFFE;
                chipsH1    = chipsH1 << 1;
                gg         = gg + 1;
                chip_index = chip_index + 1;
            } else if (chips1[chip_index] == FUZZY_CHIP) {
                chipsH1 &= 0xFFFFFFFFFFFFFFFE;
                chipsH1    = chipsH1 << 1;
                gg         = gg + 1;
                chip_index = chip_index + 1;
            } else if (chips1[chip_index] == 0x01) {
                chipsH1 |= 0x0000000000000001;
                chipsH1    = chipsH1 << 1;
                gg         = gg + 1;
                chip_index = chip_index + 2;
            }
        }
    }
    return chipsH1;
}

uint64_t _poly_check(uint32_t poly, uint32_t bits, uint8_t numbits) {
    uint64_t bits_out      = 0;
    uint8_t  shift_counter = 1;
    uint8_t  result        = 0;
    uint8_t  ii            = 0;
    uint32_t masked_buff   = 0;
    uint32_t buffer        = bits;   // mask to prevent bit overflow
    poly &= 0x00001FFFF;             // mask to prevent silliness
    bits_out |= buffer;              // initialize 17 LSBs of result
    bits_out &= 0x00000000FFFFFFFF;  // mask because I didn't want to re-cast the buffer

    while (shift_counter <= numbits) {
        bits_out    = bits_out << 1;        // shift left (forward in time) by 1
        masked_buff = ((buffer) & (poly));  // mask the buffer with the selected polynomial
        for (ii = 0; ii < 17; ii++) {
            result = result ^ ((((masked_buff)) >> ii) & (0x00000001));  // cumulative sum of buffer&poly
        }
        buffer = (buffer & 0x0000FFFF) << 1;
        buffer |= ((result) & (0x01));
        bits_out |= ((result) & (0x01));  // put result of the XOR operation into the new bit
        result = 0;
        shift_counter++;
    }
    return bits_out;
}

uint8_t _determine_polynomial(uint64_t chipsH1, int8_t *start_val) {
    // check which polynomial the bit sequence is part of
    // TODO: make function a void and modify memory directly
    // TODO: rename chipsH1 to something relevant... like bits?
    int32_t  bits_N_for_comp                     = 47;
    uint32_t bit_buffer1                         = (uint32_t)(((0xFFFF800000000000) & chipsH1) >> 47);
    uint64_t bits_from_poly[LH2_LOCATIONS_COUNT] = { 0 };
    uint64_t weights[LH2_LOCATIONS_COUNT]        = { 0xFFFFFFFFFFFFFFFF };
    uint8_t  selected_poly                       = LH2_POLYNOMIAL_ERROR_INDICATOR;  // initialize to error condition
    uint64_t bits_to_compare                     = 0;
    int32_t  threshold                           = POLYNOMIAL_BIT_ERROR_INITIAL_THRESHOLD;

    *start_val = 0;  // TODO: remove this? possible that I modify start value during the demodulation process

    // try polynomial vs. first buffer bits
    // this search takes 17-bit sequences and runs them forwards through the polynomial LFSRs.
    // if the remaining detected bits fit well with the chosen 17-bit sequence and a given polynomial, it is treated as "correct"
    // in case of bit errors at the beginning of the capture, the 17-bit sequence is shifted (to a max of 8 bits)
    // in case of bit errors at the end of the capture, the ending bits are removed (to a max of
    // removing bits reduces the threshold correspondingly, as incorrect packet detection will cause a significant delay in location estimate

    // run polynomial search on the first capture
    while (1) {
        // TODO: do this math stuff in multiple operations to: (a) make it readable (b) ensure order-of-execution
        bit_buffer1       = (uint32_t)(((0xFFFF800000000000 >> (*start_val)) & chipsH1) >> (64 - 17 - (*start_val)));
        bits_from_poly[0] = (((_poly_check(_polynomials[0], bit_buffer1, bits_N_for_comp)) << (64 - 17 - (*start_val) - bits_N_for_comp)) | (chipsH1 & (0xFFFFFFFFFFFFFFFF << (64 - (*start_val)))));
        bits_from_poly[1] = (((_poly_check(_polynomials[1], bit_buffer1, bits_N_for_comp)) << (64 - 17 - (*start_val) - bits_N_for_comp)) | (chipsH1 & (0xFFFFFFFFFFFFFFFF << (64 - (*start_val)))));
        bits_to_compare   = (chipsH1 & (0xFFFFFFFFFFFFFFFF << (64 - 17 - (*start_val) - bits_N_for_comp)));
        weights[0]        = _hamming_weight(bits_from_poly[0] ^ bits_to_compare);
        weights[1]        = _hamming_weight(bits_from_poly[1] ^ bits_to_compare);
        if (bits_N_for_comp < 10) {                          // too few bits to reliably compare, give up
            selected_poly = LH2_POLYNOMIAL_ERROR_INDICATOR;  // mark the poly as "wrong"
            break;
        }  // TODO: implement sorting network for efficiency?
        if ((weights[0] <= (uint64_t)threshold) | (weights[1] <= (uint64_t)threshold)) {
            if ((weights[0] < weights[1])) {  // weight0 is the smallest
                selected_poly = 0;
                break;
            } else if ((weights[1] < weights[0])) {  // weight1 is the smallest
                selected_poly = 1;
                break;
            }
        } else if (*start_val > 8) {  // match failed, try again removing bits from the end
            *start_val      = 0;
            bits_N_for_comp = bits_N_for_comp + 1;
            if (threshold > 1) {
                threshold = threshold - 1;
            } else if (threshold == 1) {  // keep threshold at ones, but you're probably screwed with an unlucky bit error
                threshold = 1;
            }
        } else {
            *start_val      = *start_val + 1;
            bits_N_for_comp = bits_N_for_comp - 1;
        }
    }
    return selected_poly;
}

uint64_t _hamming_weight(uint64_t bits_in) {  // TODO: bad name for function? or is it, it might be a good name for a function, because it describes exactly what it does
    uint64_t weight = bits_in;
    weight          = weight - ((weight >> 1) & 0x5555555555555555);                         // find # of 1s in every 2-bit block
    weight          = (weight & 0x3333333333333333) + ((weight >> 2) & 0x3333333333333333);  // find # of 1s in every 4-bit block
    weight          = (weight + (weight >> 4)) & 0x0F0F0F0F0F0F0F0F;                         // find # of 1s in every 8-bit block
    weight          = (weight + (weight >> 8)) & 0x00FF00FF00FF00FF;                         // find # of 1s in every 16-bit block
    weight          = (weight + (weight >> 16)) & 0x0000FFFF0000FFFF;                        // find # of 1s in every 32-bit block
    weight          = (weight + (weight >> 32));                                             // add the two 32-bit block results together
    weight          = weight & 0x000000000000007F;                                           // mask final result, max value of 64, 0'b01000000
    return weight;
}

uint32_t _reverse_count_p(uint8_t index, uint32_t bits) {
    uint32_t count       = 0;
    uint32_t buffer      = bits & 0x0001FFFFF;  // initialize buffer to initial bits, masked
    uint8_t  ii          = 0;                   // loop variable for cumulative sum
    uint32_t result      = 0;
    uint32_t b17         = 0;
    uint32_t masked_buff = 0;
    while (buffer != _end_buffers[index][0])  // do until buffer reaches one of the saved states
    {
        b17         = buffer & 0x00000001;               // save the "newest" bit of the buffer
        buffer      = (buffer & (0x0001FFFE)) >> 1;      // shift the buffer right, backwards in time
        masked_buff = (buffer) & (_polynomials[index]);  // mask the buffer w/ the selected polynomial
        for (ii = 0; ii < 17; ii++) {
            result = result ^ (((masked_buff) >> ii) & (0x00000001));  // cumulative sum of buffer&poly
        }
        result = result ^ b17;
        buffer = buffer | (result << 16);  // update buffer w/ result
        result = 0;                        // reset result
        count++;
        if ((buffer ^ _end_buffers[index][1]) == 0x00000000) {
            count  = count + 8192 - 1;
            buffer = _end_buffers[index][0];
        }
        if ((buffer ^ _end_buffers[index][2]) == 0x00000000) {
            count  = count + 16384 - 1;
            buffer = _end_buffers[index][0];
        }
        if ((buffer ^ _end_buffers[index][3]) == 0x00000000) {
            count  = count + 24576 - 1;
            buffer = _end_buffers[index][0];
        }
        if ((buffer ^ _end_buffers[index][4]) == 0x00000000) {
            count  = count + 32768 - 1;
            buffer = _end_buffers[index][0];
        }
        if ((buffer ^ _end_buffers[index][5]) == 0x00000000) {
            count  = count + 40960 - 1;
            buffer = _end_buffers[index][0];
        }
        if ((buffer ^ _end_buffers[index][6]) == 0x00000000) {
            count  = count + 49152 - 1;
            buffer = _end_buffers[index][0];
        }
        if ((buffer ^ _end_buffers[index][7]) == 0x00000000) {
            count  = count + 57344 - 1;
            buffer = _end_buffers[index][0];
        }
        if ((buffer ^ _end_buffers[index][8]) == 0x00000000) {
            count  = count + 65536 - 1;
            buffer = _end_buffers[index][0];
        }
        if ((buffer ^ _end_buffers[index][9]) == 0x00000000) {
            count  = count + 73728 - 1;
            buffer = _end_buffers[index][0];
        }
        if ((buffer ^ _end_buffers[index][10]) == 0x00000000) {
            count  = count + 81920 - 1;
            buffer = _end_buffers[index][0];
        }
        if ((buffer ^ _end_buffers[index][11]) == 0x00000000) {
            count  = count + 90112 - 1;
            buffer = _end_buffers[index][0];
        }
        if ((buffer ^ _end_buffers[index][12]) == 0x00000000) {
            count  = count + 98304 - 1;
            buffer = _end_buffers[index][0];
        }
        if ((buffer ^ _end_buffers[index][13]) == 0x00000000) {
            count  = count + 106496 - 1;
            buffer = _end_buffers[index][0];
        }
        if ((buffer ^ _end_buffers[index][14]) == 0x00000000) {
            count  = count + 114688 - 1;
            buffer = _end_buffers[index][0];
        }
        if ((buffer ^ _end_buffers[index][15]) == 0x00000000) {
            count  = count + 122880 - 1;
            buffer = _end_buffers[index][0];
        }
    }
    return count;
}

void _lh2_pin_set_input(const gpio_t *gpio) {
    // Configure Data pin as INPUT, with no pullup or pull down.
    nrf_port[gpio->port]->PIN_CNF[gpio->pin] = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
                                               (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                                               (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos);
}

void _lh2_pin_set_output(const gpio_t *gpio) {
    // Configure Data pin as OUTPUT, with standar power drive current.
    nrf_port[gpio->port]->PIN_CNF[gpio->pin] = (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) |   // Set Pin as output
                                               (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos);  // Activate high current gpio mode.
}

void _gpiote_setup(const gpio_t *gpio_e) {
    NRF_GPIOTE->CONFIG[GPIOTE_CH_IN_ENV_HiToLo] = (GPIOTE_CONFIG_MODE_Event << GPIOTE_CONFIG_MODE_Pos) |
                                                  (gpio_e->pin << GPIOTE_CONFIG_PSEL_Pos) |
                                                  (gpio_e->port << GPIOTE_CONFIG_PORT_Pos) |
                                                  (GPIOTE_CONFIG_POLARITY_HiToLo << GPIOTE_CONFIG_POLARITY_Pos);

    NRF_GPIOTE->CONFIG[GPIOTE_CH_IN_ENV_LoToHi] = (GPIOTE_CONFIG_MODE_Event << GPIOTE_CONFIG_MODE_Pos) |
                                                  (gpio_e->pin << GPIOTE_CONFIG_PSEL_Pos) |
                                                  (gpio_e->port << GPIOTE_CONFIG_PORT_Pos) |
                                                  (GPIOTE_CONFIG_POLARITY_LoToHi << GPIOTE_CONFIG_POLARITY_Pos);
}

void _ppi_setup(void) {
#if defined(NRF5340_XXAA) && defined(NRF_APPLICATION)
    NRF_GPIOTE->PUBLISH_IN[GPIOTE_CH_IN_ENV_HiToLo] = PPI_SPI_START_CHAN | (GPIOTE_PUBLISH_IN_EN_Enabled << GPIOTE_PUBLISH_IN_EN_Pos);
    NRF_SPIM->SUBSCRIBE_START                       = PPI_SPI_START_CHAN | (SPIM_SUBSCRIBE_START_EN_Enabled << SPIM_SUBSCRIBE_START_EN_Pos);

    NRF_GPIOTE->PUBLISH_IN[GPIOTE_CH_IN_ENV_LoToHi] = PPI_SPI_STOP_CHAN | (GPIOTE_PUBLISH_IN_EN_Enabled << GPIOTE_PUBLISH_IN_EN_Pos);
    NRF_SPIM->SUBSCRIBE_STOP                        = PPI_SPI_STOP_CHAN | (SPIM_SUBSCRIBE_STOP_EN_Enabled << SPIM_SUBSCRIBE_STOP_EN_Pos);
#else
    uint32_t gpiote_input_task_addr = (uint32_t)&NRF_GPIOTE->EVENTS_IN[GPIOTE_CH_IN_ENV_HiToLo];
    uint32_t envelope_input_LoToHi  = (uint32_t)&NRF_GPIOTE->EVENTS_IN[GPIOTE_CH_IN_ENV_LoToHi];
    uint32_t spi_start_task_addr    = (uint32_t)&NRF_SPIM3->TASKS_START;
    uint32_t spi_stop_task_addr     = (uint32_t)&NRF_SPIM3->TASKS_STOP;

    NRF_PPI->CH[PPI_SPI_START_CHAN].EEP = gpiote_input_task_addr;  // envelope down
    NRF_PPI->CH[PPI_SPI_START_CHAN].TEP = spi_start_task_addr;     // start spi3 transfer

    NRF_PPI->CH[3].EEP = envelope_input_LoToHi;  // envelope up, finished lh2 data
    NRF_PPI->CH[3].TEP = spi_stop_task_addr;     // stop spi3 transfer
#endif
}

void _spi_setup(const gpio_t *gpio_d) {
    // Define the necessary Pins in the SPIM peripheral
    NRF_SPIM->PSEL.MISO = gpio_d->pin << SPIM_PSEL_MISO_PIN_Pos |                          // Define pin number for MISO pin
                          gpio_d->port << SPIM_PSEL_MISO_PORT_Pos |                        // Define pin port for MISO pin
                          SPIM_PSEL_MISO_CONNECT_Connected << SPIM_PSEL_MISO_CONNECT_Pos;  // Enable the MISO pin

    NRF_SPIM->PSEL.SCK = _lh2_spi_fake_sck_gpio.pin << SPIM_PSEL_SCK_PIN_Pos |          // Define pin number for SCK pin
                         _lh2_spi_fake_sck_gpio.port << SPIM_PSEL_SCK_PORT_Pos |        // Define pin port for SCK pin
                         SPIM_PSEL_SCK_CONNECT_Connected << SPIM_PSEL_SCK_CONNECT_Pos;  // Enable the SCK pin

    NRF_SPIM->PSEL.MOSI = (4UL) << SPIM_PSEL_MOSI_PIN_Pos |
                          1 << SPIM_PSEL_MOSI_PORT_Pos |
                          SPIM_PSEL_MOSI_CONNECT_Connected << SPIM_PSEL_MOSI_CONNECT_Pos;

    // Configure the Interruptions
    NVIC_ClearPendingIRQ(SPIM_IRQ);
    NVIC_DisableIRQ(SPIM_IRQ);  // Disable interruptions while configuring

    // Configure the SPIM peripheral
    NRF_SPIM->FREQUENCY = SPIM_FREQUENCY_FREQUENCY_M32;                         // Set SPI frequency to 32MHz
    NRF_SPIM->CONFIG    = SPIM_CONFIG_ORDER_MsbFirst << SPIM_CONFIG_ORDER_Pos;  // Set MsB out first

    // Configure the EasyDMA channel, only using RX
    NRF_SPIM->RXD.MAXCNT = SPI_BUFFER_SIZE;                    // Set the size of the input buffer.
    NRF_SPIM->RXD.PTR    = (uint32_t)_lh2_vars.spi_rx_buffer;  // Set the input buffer pointer.

    NRF_SPIM->INTENSET = SPIM_INTENSET_END_Enabled << SPIM_INTENSET_END_Pos;  // Enable interruption for when a packet arrives
    NVIC_SetPriority(SPIM_IRQ, SPIM_INTERRUPT_PRIORITY);                      // Set priority for Radio interrupts to 1
    // Enable SPIM interruptions
    NVIC_EnableIRQ(SPIM_IRQ);

    // Enable the SPIM peripheral
    NRF_SPIM->ENABLE = SPIM_ENABLE_ENABLE_Enabled << SPIM_ENABLE_ENABLE_Pos;
}

//=========================== interrupts =======================================

void SPIM_IRQ_HANDLER(void) {
    // Check if the interrupt was caused by a fully send package
    if (NRF_SPIM->EVENTS_END) {
        // Clear the Interrupt flag
        NRF_SPIM->EVENTS_END = 0;
        _lh2_vars.transfer_counter++;
        // load global SPI buffer (_lh2_vars.spi_rx_buffer) into four local arrays (_lh2_vars.data[0].buffer ... _lh2_vars.data[3].buffer)
        if (_lh2_vars.transfer_counter == 1) {
            memcpy(_lh2_vars.data[0].buffer, _lh2_vars.spi_rx_buffer, SPI_BUFFER_SIZE);
        } else if (_lh2_vars.transfer_counter == 2) {
            memcpy(_lh2_vars.data[1].buffer, _lh2_vars.spi_rx_buffer, SPI_BUFFER_SIZE);
            _lh2_vars.buffers_ready = true;
        }
    }
}
