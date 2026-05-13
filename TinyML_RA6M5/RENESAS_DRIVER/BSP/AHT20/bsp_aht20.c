/**
 * @file    bsp_aht20.c
 * @brief   AHT20 temperature + humidity sensor driver.
 *
 * Protocol (AHT20 datasheet Rev 1.1):
 *
 *   Init:
 *     1. Power-on → wait 100 ms
 *     2. Read status (0x71): if bit3=0 (not calibrated) → send 0xBE 0x08 0x00
 *     3. Wait 10 ms
 *
 *   Measurement:
 *     1. Send trigger: 0xAC 0x33 0x00
 *     2. Wait ≥ 80 ms
 *     3. Read 6 bytes
 *        Byte 0   : status  (bit7=busy, bit3=calibrated)
 *        Bytes 1-2 + nibble of byte 3 : RH  raw [20 bits]
 *        Nibble of byte 3 + bytes 4-5  : T   raw [20 bits]
 *
 *   Conversion:
 *     RH (%)  = raw_rh  * 100    / 2^20
 *     T  (°C) = raw_t   * 200    / 2^20  −  50
 */

#include "bsp_aht20.h"
#include "kernel.h"
#include <stdint.h>

/* -----------------------------------------------------------------------
 * Internal constants
 * ----------------------------------------------------------------------- */
#define AHT20_CMD_STATUS         0x71U   /* Read status byte         */
#define AHT20_CMD_INIT           0xBEU   /* Initialise / calibrate   */
#define AHT20_CMD_TRIGGER        0xACU   /* Start measurement        */

#define AHT20_STATUS_BUSY_BIT    (1U << 7U)   /* 1 = sensor busy     */
#define AHT20_STATUS_CAL_BIT     (1U << 3U)   /* 1 = calibrated OK   */

/* -----------------------------------------------------------------------
 * aht20_delay_ms — hybrid delay helper.
 *
 * Before the RTOS starts, use a local busy-wait for power-on/init delays.
 * Once a task is running, use OS_Task_Delay() so sensor conversions do not
 * burn CPU time or stretch the observed task period by the busy-wait cost.
 * ----------------------------------------------------------------------- */
static void aht20_delay_ms(uint32_t ms)
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
 * aht20_read_status — read the 1-byte status register.
 * Returns the status byte, or 0xFF on NACK/error.
 * ----------------------------------------------------------------------- */
static uint8_t aht20_read_status(I2C_t i2c)
{
    uint8_t status = 0xFFU;

    I2C_Start(i2c);
    if (!I2C_Transmit_Address(i2c, AHT20_I2C_ADDR, I2C_READ))
    {
        I2C_Stop(i2c);   /* release bus before returning */
        return 0xFFU;   /* NACK — sensor not responding */
    }
    I2C_Master_Receive_Data(i2c, &status, 1U);   /* I2C_Stop called inside */
    return status;
}

/* -----------------------------------------------------------------------
 * AHT20_Init — power-on initialisation (call once after I2C_Init).
 * ----------------------------------------------------------------------- */
void AHT20_Init(I2C_t i2c)
{
    uint8_t status;
    uint8_t init_cmd[3] = { AHT20_CMD_INIT, 0x08U, 0x00U };

    /* Wait ≥ 100 ms after sensor power-on before first access */
    aht20_delay_ms(100U);

    /* Check calibration flag */
    status = aht20_read_status(i2c);

    if (!(status & AHT20_STATUS_CAL_BIT))
    {
        /* Sensor not calibrated — issue initialisation command */
        I2C_Start(i2c);
        if (I2C_Transmit_Address(i2c, AHT20_I2C_ADDR, I2C_WRITE))
        {
            I2C_Master_Transmit_Data(i2c, init_cmd, 3U);
        }
        I2C_Stop(i2c);
        aht20_delay_ms(10U);
    }
}

/* -----------------------------------------------------------------------
 * AHT20_Read — trigger measurement, wait, read result.
 * ----------------------------------------------------------------------- */
AHT20_Status_t AHT20_Read(I2C_t i2c, AHT20_Data_t *out)
{
    uint8_t  buf[6];
    uint8_t  trig_cmd[3] = { AHT20_CMD_TRIGGER, 0x33U, 0x00U };
    uint32_t raw_rh;
    uint32_t raw_t;

    /* --- Step 1: trigger measurement ---------------------------------- */
    I2C_Start(i2c);
    if (!I2C_Transmit_Address(i2c, AHT20_I2C_ADDR, I2C_WRITE))
    {
        I2C_Stop(i2c);   /* release bus — sensor not responding */
        return AHT20_ERR_NACK;
    }
    if (!I2C_Master_Transmit_Data(i2c, trig_cmd, 3U))
    {
        I2C_Stop(i2c);
        return AHT20_ERR_NACK;
    }
    I2C_Stop(i2c);

    /* --- Step 2: wait ≥ 80 ms for conversion to finish --------------- */
    aht20_delay_ms(80U);

    /* --- Step 3: read 6 bytes ---------------------------------------- */
    I2C_Start(i2c);
    if (!I2C_Transmit_Address(i2c, AHT20_I2C_ADDR, I2C_READ))
    {
        I2C_Stop(i2c);
        return AHT20_ERR_NACK;
    }
    if (!I2C_Master_Receive_Data(i2c, buf, 6U))   /* I2C_Stop inside */
    {
        return AHT20_ERR_TIMEOUT;
    }

    /* --- Step 4: check busy flag -------------------------------------- */
    if (buf[0] & AHT20_STATUS_BUSY_BIT)
    {
        return AHT20_ERR_BUSY;
    }

    /* --- Step 5: parse 20-bit raw values ------------------------------ */
    /*
     * Byte layout (AHT20 datasheet §6.3):
     *
     *  buf[0]               buf[1]         buf[2]         buf[3]
     *  [status]       [RH[19:12]]    [RH[11:4]]   [RH[3:0] | T[19:16]]
     *
     *  buf[4]               buf[5]
     *  [T[15:8]]        [T[7:0]]
     */
    raw_rh = ((uint32_t)buf[1] << 12U)
           | ((uint32_t)buf[2] <<  4U)
           | ((uint32_t)buf[3] >>  4U);

    raw_t  = ((uint32_t)(buf[3] & 0x0FU) << 16U)
           | ((uint32_t)buf[4]            <<  8U)
           |  (uint32_t)buf[5];

    /* --- Step 6: convert to physical units ---------------------------- */
    /* RH (%) = raw_rh / 2^20 × 100  */
    out->humidity_pct  = (float)raw_rh * (100.0f / 1048576.0f);

    /* T (°C) = raw_t / 2^20 × 200 − 50 */
    out->temperature_c = (float)raw_t  * (200.0f / 1048576.0f) - 50.0f;

    return AHT20_OK;
}
