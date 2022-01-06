/*********************************************************************
*                                                                    * 
*                               DotBot                               *
*                                                                    *
**********************************************************************/

/**
 * @file 00std_led.c
 * @author Said Alvarado-Marin <said-alexander.alvarado-marin@inria.fr>
 * @brief This is a short example of how to interface with the RGB LED in the DotBot board.
 * 
 * Load this program on your board. The LEDs should start blinking different colors.
 * 
 * @version 0.1
 * @date 2022-01-06
 * 
 * @copyright INRIA, 2022
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <nrf.h>


//=========================== defines =========================================
// Pin definitions
#define MOSI_PIN 3   ///< nRF52840 P0.3
#define SCK_PIN 6    ///< nRF52840 P1.6 ( used because it's not an available pin in the BCM module).

// EasyDMA buffer size definition
#define READERBUFFER_SIZE 32
#define WRITERBUFFER_SIZE 32

// Define the ONE and ZERO enconding for the led one-wire protocol
#define LED_ONE  0b00010100
#define LED_ZERO 0b00010000


//=========================== variables =========================================
// EasyDMA buffer declaration (preloaded with a RED led command, just as an example, it's not actually used)
uint8_t readerBuffer[READERBUFFER_SIZE];
uint8_t writerBuffer[WRITERBUFFER_SIZE] = {0x00, 0x84, 0x29, 0x4A, 0x42, 0x90, 0xA4, 0x29, 0x08, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0xA5, 0x29, 0x4A, 0x52, 0x94, 0xA5, 0x29, 0x48, 0x42, 0x10, 0x84, 0x21, 0x08, 0x42, 0x10, 0x00};

// EasyDMA buffer declaration for the RGB LED.
uint8_t ledBuffer[WRITERBUFFER_SIZE];


//=========================== prototypes =========================================
void set_rgb_led(uint8_t r, uint8_t g, uint8_t b);


//=========================== public =========================================

/**
*  @brief The program starts executing here.
*/
int main(void)
{

  // Turn ON the DotBot board regulator 
  NRF_P0->DIRSET = 1 << 20;  
  NRF_P0->OUTSET = 1 << 20;   

  // Configure the necessary Pins in the GPIO peripheral 
  NRF_P0->DIRSET = 1 << MOSI_PIN;  // MOSI as Output
  //NRF_P0->DIRCLR = 1 << MISO_PIN;  // MISO as Input
  NRF_P0->DIRSET = 1 << SCK_PIN;   // SCK  as Output
  //NRF_P0->DIRSET = 1 << CS_PIN;    // CS   as Output

  // Define the necessary Pins in the SPIM peripheral 
  NRF_SPIM0->PSEL.MOSI =  MOSI_PIN << SPIM_PSEL_MOSI_PIN_Pos  |                           // Define pin number for MOSI pin
                          0        << SPIM_PSEL_MOSI_PORT_Pos |                           // Define pin port for MOSI pin
                          SPIM_PSEL_MOSI_CONNECT_Connected << SPIM_PSEL_MOSI_CONNECT_Pos; // Enable the MOSI pin

  NRF_SPIM0->PSEL.SCK =  SCK_PIN  << SPIM_PSEL_SCK_PIN_Pos  |                             // Define pin number for SCK pin
                         1        << SPIM_PSEL_SCK_PORT_Pos |                             // Define pin port for SCK pin
                         SPIM_PSEL_SCK_CONNECT_Connected << SPIM_PSEL_SCK_CONNECT_Pos;    // Enable the SCK pin

  // Configure the SPIM peripheral 
  NRF_SPIM0->FREQUENCY = SPIM_FREQUENCY_FREQUENCY_K500;                        // Set SPI frequency to 500Khz
  NRF_SPIM0->CONFIG    = SPIM_CONFIG_ORDER_MsbFirst << SPIM_CONFIG_ORDER_Pos;  // Set MsB out first

  // Configure the EasyDMA channel 
  // Configuring the READER channel
  NRF_SPIM0->RXD.MAXCNT = READERBUFFER_SIZE;              // Set the size of the input buffer.
  NRF_SPIM0->RXD.PTR    = &readerBuffer;                  // Set the input buffer pointer.
  // Configure the WRITER channel
  NRF_SPIM0->TXD.MAXCNT = WRITERBUFFER_SIZE;              // Set the size of the output buffer.
  NRF_SPIM0->TXD.PTR    = &writerBuffer;                  // Set the output buffer pointer.

  // Enable the SPIM pripheral 
  NRF_SPIM0->ENABLE = SPIM_ENABLE_ENABLE_Enabled << SPIM_ENABLE_ENABLE_Pos;

  // Set color of the LED
  set_rgb_led(0,50,50);

  // Execute a transfer 
  NRF_SPIM0->TASKS_START = SPIM_TASKS_START_TASKS_START_Trigger << SPIM_TASKS_START_TASKS_START_Pos;  // trigger the SPI transfer

  // one last instruction, doesn't do anything, it's just to have a place to put a breakpoint.
  NRF_P0->DIRSET = 1 << 14;
}

/**
 * @brief Set the color to display in the RGB LED
 * 
 *  Assembles the 1-wire encoded command to change the color of the RGB led.
 *  Afterward, an SPI transfer is necessary to actually update the LED color.
 * 
 * @param[in] r red value of the led color [0 - 255]
 * @param[in] g green value of the led color [0 - 255]
 * @param[in] b blue value of the led color [0 - 255]
 */
void set_rgb_led(uint8_t r, uint8_t g, uint8_t b){

 
  // Load the obligatory starting write command on to the buffer.
  ledBuffer[0] = 0x00;
  ledBuffer[1] = 0x84;
  ledBuffer[2] = 0x29;
  ledBuffer[3] = 0x4A;
  ledBuffer[4] = 0x42;
  ledBuffer[5] = 0x90;
  ledBuffer[6] = 0xA4;
  ledBuffer[7] = 0x29;
  ledBuffer[8] = 0x00;


  // ********** Load the Blue value into the buffer ************

  // Define the starting position of the blue color in the buffer.
  int buffer_byte = 8;
  int buffer_bit  = 59;

  // Iterate over all the bits of the blue color. (decreasing order, we write MSB first.)
  for (int i = 7; i >= 0; i--) 
  {                                                 
    // Extract 1 bit from the blue color.
    int blue_bit = (b >> i) & 0x01;
    // if the current bit is a one, copy the 5bits that represent a '1'. in the one-wire-protocol, on to the correct position in the buffer.
    if (blue_bit) {
      // iterate over all the bits in the 5bit encoded '1'.
      for (int j = 4; j >= 0; j--) 
      {
        // Extract a single (j) bit from the '1' code, left shift it to the corect position, and write into the buffer.
        ledBuffer[buffer_byte] |= ((LED_ONE >> j) & 0x01) << (buffer_bit % 8);
        // move the buffer bit position we are writting to, a single bit to the right.
        buffer_bit--;
        // if the next bit to write wraps over to the next byte, increment the buffer byte position.
        if ((buffer_bit % 8) == 7) buffer_byte++;                               
      }
    }
    // However, if the current bit is a zero, repeat the above process but, with the 5bit encoded '0'
    else {
      for (int j = 4; j >= 0; j--) 
      {
        ledBuffer[buffer_byte] |= ((LED_ZERO >> j) & 0x01) << (buffer_bit % 8);
        buffer_bit--;
        if ((buffer_bit % 8) == 7)
          buffer_byte++;
      }
    }
  }
  // fill the remaining 4 bytes of the 1-wire command with encoded zeros
  ledBuffer[13] |= 0x08;
  ledBuffer[14] = 0x42;
  ledBuffer[15] = 0x10;



  // ********** Load the Red value into the buffer ************

  // Define the starting position of the blue color in the buffer.
  buffer_byte = 16;
  buffer_bit = 63;

  // Iterate over all the bits of the red color. (decreasing order, we write MSB first.)
  for (int i = 7; i >= 0; i--)
  {
    // Extract 1 bit from the red color.
    int red_bit = (r >> i) & 0x01;
    // if the current bit is a one, copy the 5bits that represent a '1'. in the one-wire-protocol, on to the correct position in the buffer.
    if (red_bit)
    {
      // iterate over all the bits in the 5bit encoded '1'.
      for (int j = 4; j >= 0; j--)
      {
        // Extract a single (j) bit from the '1' code, left shift it to the corect position, and write into the buffer.
        ledBuffer[buffer_byte] |= ((LED_ONE >> j) & 0x01) << (buffer_bit % 8);
        // move the buffer bit position we are writting to, a single bit to the right.
        buffer_bit--;
        // if the next bit to write wraps over to the next byte, increment the buffer byte position.
        if ((buffer_bit % 8) == 7)
          buffer_byte++;
      }
    }
    // However, if the current bit is a zero, repeat the above process but, with the 5bit encoded '0'
    else
    {
      for (int j = 4; j >= 0; j--)
      {
        ledBuffer[buffer_byte] |= ((LED_ZERO >> j) & 0x01) << (buffer_bit % 8);
        buffer_bit--;
        if ((buffer_bit % 8) == 7)
          buffer_byte++;
      }
    }
  }
  // fill the remaining 4 bytes of the 1-wire command with encoded zeros
  ledBuffer[21] = 0x84;
  ledBuffer[22] = 0x21;



  // ********** Load the green into the buffer ************

  // Define the starting position of the blue color in the buffer.
  buffer_byte = 23;
  buffer_bit = 59;

  // Iterate over all the bits of the green color. (decreasing order, we write MSB first.)
  for (int i = 7; i >= 0; i--)
  {
    // Extract 1 bit from the green color.
    int green_bit = (g >> i) & 0x01;
    // if the current bit is a one, copy the 5bits that represent a '1'. in the one-wire-protocol, on to the correct position in the buffer.
    if (green_bit)
    {
      // iterate over all the bits in the 5bit encoded '1'.
      for (int j = 4; j >= 0; j--)
      {
        // Extract a single (j) bit from the '1' code, right left it to the corect position, and write into the buffer.
        ledBuffer[buffer_byte] |= ((LED_ONE >> j) & 0x01) << (buffer_bit % 8);
        // move the buffer bit position we are writting to, a single bit to the right.
        buffer_bit--;
        // if the next bit to write wraps over to the next byte, increment the buffer byte position.
        if ((buffer_bit % 8) == 7)
          buffer_byte++;
      }
    }
    // However, if the current bit is a zero, repeat the above process but, with the 5bit encoded '0'
    else
    {
      for (int j = 4; j >= 0; j--)
      {
        ledBuffer[buffer_byte] |= ((LED_ZERO >> j) & 0x01) << (buffer_bit % 8);
        buffer_bit--;
        if ((buffer_bit % 8) == 7)
          buffer_byte++;
      }
    }
  }

  // fill the remaining 4 bytes of the 1-wire command with encoded zeros
  ledBuffer[28] |= 0x08;
  ledBuffer[29]  = 0x42;
  ledBuffer[30]  = 0x10;

  // ********** Load buffer into SPI register ************
  // Configure the WRITER channel
  NRF_SPIM0->TXD.MAXCNT = WRITERBUFFER_SIZE; // Set the size of the input buffer.
  NRF_SPIM0->TXD.PTR = &ledBuffer;        // Set the input buffer pointer.
}

/*************************** End of file ****************************/
