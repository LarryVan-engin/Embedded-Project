/**
 * @file    bsp_hs3001.c
 * @brief   HS3001 temperature + humidity sensor driver.
 *
 * Protocol:
 *
 *   Measurement Request (MR):
 *     1. Master sends I2C Start, 7-bit Address (0x44) with Write bit.
 *     2. Sensor ACKs. Master sends STOP.
 *     3. Wait >= 1.2 ms for measurement to complete.
 *
 *   Data Fetch (DF):
 *     1. Master sends I2C Start, 7-bit Address (0x44) with Read bit.
 *     2. Master reads 4 bytes from sensor.
 *        Byte 0   : status (top 2 bits), Humidity MSB (lower 6 bits)
 *        Byte 1   : Humidity LSB
 *        Byte 2   : Temperature MSB
 *        Byte 3   : Temperature LSB (top 6 bits, bottom 2 bits ignored)
 *
 *   Status Bits (Byte 0 [7:6]):
 *     00 = Valid data
 *     01 = Stale data (already read)
 *     10 = Command mode (should not happen during normal operation)
 *     11 = Not used
 *
 *   Conversion:
 *     RH (%)  = (raw_rh / (2^14 - 1)) * 100
 *     T  (°C) = (raw_t / (2^14 - 1)) * 165 - 40
 */

#include "bsp_hs3001.h"
#include "kernel.h"
#include <stdint.h>

/* -----------------------------------------------------------------------
 * Internal constants
 * ----------------------------------------------------------------------- */
#define HS3001_STATUS_MASK       0xC0U
#define HS3001_STATUS_VALID      0x00U
#define HS3001_STATUS_STALE      0x40U

/* -----------------------------------------------------------------------
 * hs3001_delay_ms — hybrid delay helper.
 * ----------------------------------------------------------------------- */
static void hs3001_delay_ms(uint32_t ms)
{
    if ((os_current_task != (OS_TCB_t *)0) && (ms != 0U))
    {
        OS_Task_Delay(ms);
        return;
    }

    volatile uint32_t n = ms * 100000U;
    while (n-- != 0U)
    {
        __asm volatile("nop");
    }
}

/* -----------------------------------------------------------------------
 * HS3001_Init — power-on initialisation.
 * ----------------------------------------------------------------------- */
void HS3001_Init(I2C_t i2c)
{
    /* Wait >= 100 ms after sensor power-on before first access */
    hs3001_delay_ms(100U);

    /* Verify sensor responds by doing a quick measurement request */
    I2C_Start(i2c);
    if (I2C_Transmit_Address(i2c, HS3001_I2C_ADDR, I2C_WRITE))
    {
        /* Sensor is present */
    }
    I2C_Stop(i2c);
}

/* -----------------------------------------------------------------------
 * HS3001_Read — trigger measurement, wait, read result.
 * ----------------------------------------------------------------------- */
HS3001_Status_t HS3001_Read(I2C_t i2c, HS3001_Data_t *out)
{
    uint8_t  buf[4];
    uint16_t raw_rh;
    uint16_t raw_t;
    uint8_t  status;

    /* --- Step 1: Measurement Request (MR) ----------------------------- */
    I2C_Start(i2c);
    if (!I2C_Transmit_Address(i2c, HS3001_I2C_ADDR, I2C_WRITE))
    {
        I2C_Stop(i2c);   /* release bus — sensor not responding */
        return HS3001_ERR_NACK;
    }
    I2C_Stop(i2c);

    /* --- Step 2: wait >= 2 ms for conversion to finish ---------------- */
    hs3001_delay_ms(2U);

    /* --- Step 3: Data Fetch (DF) - read 4 bytes ----------------------- */
    I2C_Start(i2c);
    if (!I2C_Transmit_Address(i2c, HS3001_I2C_ADDR, I2C_READ))
    {
        I2C_Stop(i2c);
        return HS3001_ERR_NACK;
    }
    if (!I2C_Master_Receive_Data(i2c, buf, 4U))   /* I2C_Stop inside */
    {
        return HS3001_ERR_TIMEOUT;
    }

    /* --- Step 4: check status ----------------------------------------- */
    status = buf[0] & HS3001_STATUS_MASK;
    if (status == HS3001_STATUS_STALE)
    {
        return HS3001_ERR_STALE;
    }

    /* --- Step 5: parse 14-bit raw values ------------------------------ */
    /* Humidity: upper 6 bits of buf[0], 8 bits of buf[1] */
    raw_rh = (uint16_t)(((uint16_t)(buf[0] & 0x3FU) << 8U) | buf[1]);

    /* Temperature: 8 bits of buf[2], upper 6 bits of buf[3] */
    raw_t  = (uint16_t)(((uint16_t)buf[2] << 6U) | (buf[3] >> 2U));

    /* --- Step 6: convert to physical units ---------------------------- */
    /* RH (%) = (raw_rh / (2^14 - 1)) * 100 */
    out->humidity_pct  = ((float)raw_rh / 16383.0f) * 100.0f;

    /* T (°C) = (raw_t / (2^14 - 1)) * 165 - 40 */
    out->temperature_c = ((float)raw_t  / 16383.0f) * 165.0f - 40.0f;

    return HS3001_OK;
}
