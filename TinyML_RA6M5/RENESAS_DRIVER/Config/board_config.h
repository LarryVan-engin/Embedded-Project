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
#define BOARD_TYPE      BOARD_TYPE_EK

/* -----------------------------------------------------------------------
 * Board-Specific Pin Definitions (UART, SPI, I2C, etc.)
 * ----------------------------------------------------------------------- */

#if (BOARD_TYPE == BOARD_TYPE_CK)
  /* CK-RA6M5: Cortex-M33 Cloud Kit with Arduino UNO connectors
   *
   * VCOM path (preferred for debug):
   *   J-Link OB (R7FA4M2AB3CNE) exposes a Virtual COM Port over the same
   *   USB cable used for programming/debugging.
   *   The OB chip's VCOM_TxD drives P708 (MCU RXD1) and the OB chip reads
   *   from P709 (MCU TXD1), both on UART1 / SCI1.
   *
   *   Use any serial terminal (PuTTY, Tera Term, minicom) on the J-Link
   *   OB virtual COM port at 115200 8N1.  No additional USB-UART adapter
   *   is required.
   *
   * Arduino D0/D1 (P706/P707, SCI3) are still available for the ESP32
   *   UART bridge, but they are NOT used for debug print.
   */
  #define VCOM_UART_CHANNEL   1     /* SCI1: P709(TXD) / P708(RXD) → J-Link OB VCOM */
  #define DEBUG_UART_CHANNEL  VCOM_UART_CHANNEL
  #define DEBUG_UART_RX_PORT    7
  #define DEBUG_UART_RX_PIN     8   /* P708  ← VCOM_TxD from J-Link OB */
  #define DEBUG_UART_TX_PORT    7
  #define DEBUG_UART_TX_PIN     9   /* P709  → VCOM_RxD into J-Link OB */
  #define DEBUG_UART_PSEL       0x05U
  
  /* LED assignments (active-HIGH) */
  #define LED1_PORT  GPIO_PORT6
  #define LED1_PIN   10  /* P610 Red    */
  #define LED2_PORT  GPIO_PORT6
  #define LED2_PIN   3   /* P603 Green  */
  #define LED3_PORT  GPIO_PORT6
  #define LED3_PIN   9   /* P609 Green  */
  
  /* Sensor configuration: REAL sensors */
  #define USE_REAL_SENSORS    1
  #define USE_SENSOR_HS3001   1
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
  
  /* LED assignments (active-HIGH) */
  #define LED1_PORT  GPIO_PORT0
  #define LED1_PIN   6   /* P006 */
  #define LED2_PORT  GPIO_PORT0
  #define LED2_PIN   7   /* P007 */
  #define LED3_PORT  GPIO_PORT0
  #define LED3_PIN   8   /* P008 */
  
  /* Sensor configuration: SIMULATOR (no physical sensors on EK) */
  #define USE_REAL_SENSORS    0
  #define USE_SENSOR_HS3001   0
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

/* Helper macros to identify board */
#define IS_BOARD_CK()   (BOARD_TYPE == BOARD_TYPE_CK)
#define IS_BOARD_EK()   (BOARD_TYPE == BOARD_TYPE_EK)

#endif /* BOARD_CONFIG_H */
