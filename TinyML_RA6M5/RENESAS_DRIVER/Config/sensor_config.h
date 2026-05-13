/*
 * sensor_config.h — Unified sensor configuration based on board type.
 *
 * - CK-RA6M5: Real sensors (AHT20, ZMOD4410)
 * - EK-RA6M5: Sensor simulator (no physical sensors)
 *
 * This header is included by sensor initialization code to conditionally
 * select between real and simulated sensor backends.
 */

#ifndef SENSOR_CONFIG_H
#define SENSOR_CONFIG_H

#include "board_config.h"

/* -----------------------------------------------------------------------
 * Sensor Selection Based on Board Type
 * ----------------------------------------------------------------------- */

#if (BOARD_TYPE == BOARD_TYPE_CK)
  /* CK-RA6M5: Real sensors attached */
  #define USE_REAL_SENSORS    1
  #define USE_SENSOR_AHT20    1
  #define USE_SENSOR_ZMOD4410 1
  #define USE_SENSOR_SIMULATOR 0

#elif (BOARD_TYPE == BOARD_TYPE_EK)
  /* EK-RA6M5: No physical sensors, use simulator */
  #define USE_REAL_SENSORS    0
  #define USE_SENSOR_AHT20    0
  #define USE_SENSOR_ZMOD4410 0
  #define USE_SENSOR_SIMULATOR 1

#else
  #error "BOARD_TYPE not defined in board_config.h"
#endif

/* -----------------------------------------------------------------------
 * Helper Macros
 * ----------------------------------------------------------------------- */
#define IS_USING_REAL_SENSORS() (USE_REAL_SENSORS)
#define IS_USING_SIMULATOR()    (USE_SENSOR_SIMULATOR)

/* -----------------------------------------------------------------------
 * I2C/SPI Sensor Addresses and Pins (same for both boards)
 * ----------------------------------------------------------------------- */

/* I2C1 (RIIC1) bus configuration */
#define I2C_SENSOR_BUS        I2C1
#define I2C_SENSOR_SPEED      I2C_SPEED_STANDARD  /* 100 kHz */

/* AHT20 Humidity/Temperature Sensor */
#define AHT20_I2C_ADDRESS     0x38U
#define AHT20_ENABLED         (USE_SENSOR_AHT20)

/* ZMOD4410 Air Quality Sensor */
#define ZMOD4410_I2C_ADDRESS  0x33U
#define ZMOD4410_ENABLED      (USE_SENSOR_ZMOD4410)

/* Sensor Simulator (used on EK only) */
#define SENSOR_SIMULATOR_ENABLED (USE_SENSOR_SIMULATOR)

#endif /* SENSOR_CONFIG_H */
