/*
 * board_config.h — Unified board configuration for RA6M5 CK and EK boards.
 *
 * Set BOARD_TYPE to select target board:
 *   BOARD_TYPE_CK  = CK-RA6M5 (Cortex-M33 Cloud Kit)
 *   BOARD_TYPE_EK  = EK-RA6M5 (Evaluation Kit)
 *
 * This header ensures all drivers use the same pin mapping and sensor configuration
 * across both boards.
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdint.h>
#include "GPIO.h"

/* -----------------------------------------------------------------------
 * SELECT TARGET BOARD HERE
 * ----------------------------------------------------------------------- */
#define BOARD_TYPE_CK   1
#define BOARD_TYPE_EK   2

/* Change this to switch board target */
#define BOARD_TYPE      BOARD_TYPE_CK

/* -----------------------------------------------------------------------
 * Board-Specific Pin Definitions (UART, SPI, I2C, etc.)
 * ----------------------------------------------------------------------- */

#if (BOARD_TYPE == BOARD_TYPE_CK)
  /* CK-RA6M5: Cortex-M33 Cloud Kit with Arduino UNO connectors
   *
   * Current debug route in this worktree:
   *   SCI3 on Arduino D0/D1.
   *   P706 = RXD3 = Arduino D0
   *   P707 = TXD3 = Arduino D1
   *
   * J-Link OB VCOM is still available on SCI1:
   *   P708 = RXD1 from the OB bridge
   *   P709 = TXD1 to the OB bridge
   *
   * The VCOM_UART_CHANNEL macro name is kept for compatibility, but the
   * active channel value below is SCI3.
   */ 
  #define VCOM_UART_CHANNEL   3     /* Active debug route: SCI3 on Arduino D1/D0 */
  #define DEBUG_UART_CHANNEL  VCOM_UART_CHANNEL
  #define DEBUG_UART_RX_PORT    7
  #define DEBUG_UART_RX_PIN     6   /* P706 = Arduino D0 = RXD3 */
  #define DEBUG_UART_TX_PORT    7
  #define DEBUG_UART_TX_PIN     7   /* P707 = Arduino D1 = TXD3 */
  #define DEBUG_UART_PSEL       0x05U

  /* Shared debug + ESP32 bridge / OTA route: same SCI3 on Arduino D0/D1. */
  #define SERVER_COMM_UART_CHANNEL  DEBUG_UART_CHANNEL
  #define SERVER_COMM_UART_RX_PORT  DEBUG_UART_RX_PORT
  #define SERVER_COMM_UART_RX_PIN   DEBUG_UART_RX_PIN
  #define SERVER_COMM_UART_TX_PORT  DEBUG_UART_TX_PORT
  #define SERVER_COMM_UART_TX_PIN   DEBUG_UART_TX_PIN
  #define SERVER_COMM_UART_PSEL     DEBUG_UART_PSEL
  
  /* LED assignments (active-HIGH) */
  #define LED1_PORT  GPIO_PORT6
  #define LED1_PIN   10  /* P610 Red    */
  #define LED2_PORT  GPIO_PORT6
  #define LED2_PIN   3   /* P603 Green  */
  #define LED3_PORT  GPIO_PORT6
  #define LED3_PIN   9   /* P609 Green  */
  
  /* Sensor configuration: REAL sensors */
  #define USE_REAL_SENSORS    1
  #define USE_SENSOR_AHT20    1
  #define USE_SENSOR_ZMOD4410 1
  #define USE_SENSOR_SIMULATOR 0

#elif (BOARD_TYPE == BOARD_TYPE_EK)
  /* EK-RA6M5: Evaluation Kit with standard debug header */
  
  /* UART7 debug console: debug header (P614/P613) */
  #define DEBUG_UART_CHANNEL    7
  #define DEBUG_UART_RX_PORT    6
  #define DEBUG_UART_RX_PIN     14  /* P614 */
  #define DEBUG_UART_TX_PORT    6
  #define DEBUG_UART_TX_PIN     13  /* P613 */
  #define DEBUG_UART_PSEL       0x05U

  #define SERVER_COMM_UART_CHANNEL  DEBUG_UART_CHANNEL
  #define SERVER_COMM_UART_RX_PORT  DEBUG_UART_RX_PORT
  #define SERVER_COMM_UART_RX_PIN   DEBUG_UART_RX_PIN
  #define SERVER_COMM_UART_TX_PORT  DEBUG_UART_TX_PORT
  #define SERVER_COMM_UART_TX_PIN   DEBUG_UART_TX_PIN
  #define SERVER_COMM_UART_PSEL     DEBUG_UART_PSEL
  
  /* LED assignments (active-HIGH) */
  #define LED1_PORT  GPIO_PORT0
  #define LED1_PIN   6   /* P006 */
  #define LED2_PORT  GPIO_PORT0
  #define LED2_PIN   7   /* P007 */
  #define LED3_PORT  GPIO_PORT0
  #define LED3_PIN   8   /* P008 */
  
  /* Sensor configuration: SIMULATOR (no physical sensors on EK) */
  #define USE_REAL_SENSORS    0
  #define USE_SENSOR_AHT20    0
  #define USE_SENSOR_ZMOD4410 0
  #define USE_SENSOR_SIMULATOR 1

#else
  #error "BOARD_TYPE not defined or invalid. Set to BOARD_TYPE_CK or BOARD_TYPE_EK"
#endif

/* -----------------------------------------------------------------------
 * Board-Independent Settings (same for both boards)
 * ----------------------------------------------------------------------- */
#define DEBUG_UART_BAUDRATE   115200U
#define DEBUG_UART_BRR_DIV    4U     /* /4 mode: BGDM=1, ABCS=1 */
#define SERVER_COMM_UART_BAUDRATE  115200U

/* Helper macros to identify board */
#define IS_BOARD_CK()   (BOARD_TYPE == BOARD_TYPE_CK)
#define IS_BOARD_EK()   (BOARD_TYPE == BOARD_TYPE_EK)

#endif /* BOARD_CONFIG_H */