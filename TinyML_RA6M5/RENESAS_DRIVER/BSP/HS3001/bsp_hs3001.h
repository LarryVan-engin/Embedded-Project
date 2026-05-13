/**
 * @file    bsp_hs3001.h
 * @brief   BSP driver for HS3001 temperature + humidity sensor (I2C).
 *
 * Sensor: Renesas HS3001
 * Interface: I2C, 7-bit address 0x44, 100 kHz or 400 kHz
 * Requires: drv_i2c driver (I2C_Init must be called before HS3001_Init)
 *
 * Typical usage:
 *
 *   I2C_Init(I2C0, 50U, I2C_SPEED_STANDARD);   // 50 MHz PCLKB, 100 kHz
 *   HS3001_Init(I2C0);
 *
 *   HS3001_Data_t data;
 *   if (HS3001_Read(I2C0, &data) == HS3001_OK) {
 *       debug_print("T=%.1f C  RH=%.1f%%\r\n",
 *                   (double)data.temperature_c,
 *                   (double)data.humidity_pct);
 *   }
 *
 * Hardware connections (CK-RA6M5):
 *   SCL → P400 (I2C0)
 *   SDA → P401 (I2C0)
 */

#ifndef BSP_HS3001_H
#define BSP_HS3001_H

#include "drv_i2c.h"
#include <stdint.h>

/* -----------------------------------------------------------------------
 * HS3001 fixed I2C address (7-bit, not shifted)
 * ----------------------------------------------------------------------- */
#define HS3001_I2C_ADDR   0x44U

/* -----------------------------------------------------------------------
 * Return status codes
 * ----------------------------------------------------------------------- */
typedef enum {
    HS3001_OK          = 0,   /* Measurement complete and valid      */
    HS3001_ERR_NACK    = 1,   /* Sensor did not ACK — check wiring   */
    HS3001_ERR_STALE   = 2,   /* Data not updated since last read    */
    HS3001_ERR_TIMEOUT = 3    /* I2C bus timeout                     */
} HS3001_Status_t;

/* -----------------------------------------------------------------------
 * Measurement result
 * ----------------------------------------------------------------------- */
typedef struct {
    float temperature_c;   /* Temperature in degrees Celsius   (-40 to +125) */
    float humidity_pct;    /* Relative humidity in percent     (0 to 100)    */
} HS3001_Data_t;

/* -----------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */

/**
 * HS3001_Init — one-time sensor initialisation.
 *
 * Checks if the sensor responds on the I2C bus.
 *
 * @param i2c  I2C channel (I2C0, I2C1, or I2C2)
 */
void HS3001_Init(I2C_t i2c);

/**
 * HS3001_Read — trigger a measurement and return the result.
 *
 * Blocking: sends a Measurement Request, waits ≥1.2 ms, then reads
 * 4 raw bytes and converts them to physical units.
 *
 * @param i2c  I2C channel (must match the channel used in HS3001_Init)
 * @param out  Pointer to HS3001_Data_t; filled on HS3001_OK return
 * @return     HS3001_OK on success; HS3001_ERR_* on failure
 */
HS3001_Status_t HS3001_Read(I2C_t i2c, HS3001_Data_t *out);

#endif /* BSP_HS3001_H */
