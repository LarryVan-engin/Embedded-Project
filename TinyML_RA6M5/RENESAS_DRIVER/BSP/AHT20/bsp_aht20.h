/**
 * @file    bsp_aht20.h
 * @brief   BSP driver for AHT20 temperature + humidity sensor (I2C).
 *
 * Sensor: ASAIR AHT20
 * Interface: I2C, 7-bit address 0x38, 100 kHz or 400 kHz
 * Requires: drv_i2c driver (I2C_Init must be called before AHT20_Init)
 *
 * Typical usage:
 *
 *   I2C_Init(I2C0, 8U, I2C_SPEED_STANDARD);   // 8 MHz PCLKB, 100 kHz
 *   AHT20_Init(I2C0);
 *
 *   AHT20_Data_t data;
 *   if (AHT20_Read(I2C0, &data) == AHT20_OK) {
 *       debug_print("T=%.1f C  RH=%.1f%%\r\n",
 *                   (double)data.temperature_c,
 *                   (double)data.humidity_pct);
 *   }
 *
 * Hardware connections (EK-RA6M5, I2C1 — J24 connector):
 *   SCL → P512  (J24-10)
 *   SDA → P511  (J24-9)
 *   External pull-up resistors (4.7 kΩ to 3.3 V) required on SCL and SDA.
 */

#ifndef BSP_AHT20_H
#define BSP_AHT20_H

#include "drv_i2c.h"
#include <stdint.h>

/* -----------------------------------------------------------------------
 * AHT20 fixed I2C address (7-bit, not shifted)
 * ----------------------------------------------------------------------- */
#define AHT20_I2C_ADDR   0x38U

/* -----------------------------------------------------------------------
 * Return status codes
 * ----------------------------------------------------------------------- */
typedef enum {
    AHT20_OK          = 0,   /* Measurement complete and valid      */
    AHT20_ERR_NACK    = 1,   /* Sensor did not ACK — check wiring   */
    AHT20_ERR_BUSY    = 2,   /* Sensor still measuring — retry      */
    AHT20_ERR_TIMEOUT = 3    /* I2C bus timeout                     */
} AHT20_Status_t;

/* -----------------------------------------------------------------------
 * Measurement result
 * ----------------------------------------------------------------------- */
typedef struct {
    float temperature_c;   /* Temperature in degrees Celsius   (-40 to +85) */
    float humidity_pct;    /* Relative humidity in percent     (0 to 100)   */
} AHT20_Data_t;

/* -----------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */

/**
 * AHT20_Init — one-time sensor initialisation.
 *
 * Must be called once after I2C_Init and ≥100 ms after power-on.
 * Checks the calibration flag; if unset, issues the 0xBE init command.
 *
 * @param i2c  I2C channel (I2C0, I2C1, or I2C2)
 */
void AHT20_Init(I2C_t i2c);

/**
 * AHT20_Read — trigger a measurement and return the result.
 *
 * Blocking: sends the 0xAC trigger command, waits ≥80 ms, then reads
 * 6 raw bytes and converts them to physical units.
 *
 * @param i2c  I2C channel (must match the channel used in AHT20_Init)
 * @param out  Pointer to AHT20_Data_t; filled on AHT20_OK return
 * @return     AHT20_OK on success; AHT20_ERR_* on failure
 */
AHT20_Status_t AHT20_Read(I2C_t i2c, AHT20_Data_t *out);

#endif /* BSP_AHT20_H */
