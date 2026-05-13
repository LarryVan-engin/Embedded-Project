/**
 * @file    bsp_zmod4410.c
 * @brief   Lightweight ZMOD4410 gas sensor driver (I2C).
 *
 * Facts aligned with Renesas' published ZMOD4xxx/FSP stack:
 *   - 7-bit slave address: 0x32
 *   - PID register:        0x00 (2 bytes, expected PID 0x2310)
 *   - Command register:    0x93 (write 0x80 to start a measurement)
 *   - Status register:     0x94 (bit7 = sequencer running)
 *   - Result register:     0x97 (raw ADC/result bytes)
 *   - Mode-specific setup normally writes configuration blocks to
 *     0x40 / 0x50 / 0x60 / 0x68 before measurements.
 *
 * This BSP does not yet embed the Renesas mode data sets or host-side
 * IAQ algorithm. It therefore focuses on verified low-level behavior:
 * PID check, trigger command, status polling, raw result capture, and
 * cached ambient-condition input for a future algorithm layer.
 */

#include "bsp_zmod4410.h"
#include "GPIO.h"
#include "kernel.h"
#include <stdint.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Internal constants and register addresses
 * ----------------------------------------------------------------------- */
#define ZMOD4410_REG_PID                0x00U   /* Product ID MSB/LSB       */
#define ZMOD4410_REG_CMD                0x93U   /* Command register         */
#define ZMOD4410_REG_STATUS             0x94U   /* Status register          */
#define ZMOD4410_REG_RESULT_DATA        0x97U   /* Raw ADC/result register  */

#define ZMOD4410_STATUS_BUSY_BIT        (1U << 7U)  /* 1 = sequencer running */

#define ZMOD4410_CMD_TRIGGER            0x80U   /* Start measurement        */

/* CK-RA6M5 sensor RESET_N pin from board table: P307 (active-low). */
#define ZMOD4410_RESET_PORT             GPIO_PORT3
#define ZMOD4410_RESET_PIN              7U

/* Sample rates for different operation modes (in seconds) */
#define ZMOD4410_SAMPLE_TIME_IAQ_GEN2   3U      /* 3 second sample          */
#define ZMOD4410_SAMPLE_TIME_IAQ_ULP    90U     /* 90 second sample (ULP)   */
#define ZMOD4410_SAMPLE_TIME_PBAQ       5U      /* 5 second sample (PBAQ)   */
#define ZMOD4410_SAMPLE_TIME_REL_IAQ    3U      /* 3 second sample (relative)*/

/* Warm-up sample counts before valid data */
#define ZMOD4410_WARMUP_SAMPLES_IAQ_GEN2  100U
#define ZMOD4410_WARMUP_SAMPLES_ULP        10U
#define ZMOD4410_WARMUP_SAMPLES_PBAQ       60U
#define ZMOD4410_WARMUP_SAMPLES_REL_IAQ   100U

/* Internal state tracking */
static ZMOD4410_OpMode_t g_operation_mode = IAQ_2ND_GEN;
static uint32_t g_measurement_count = 0U;
static float g_ambient_temperature_c = 23.0f;
static float g_ambient_humidity_pct = 50.0f;

static void zmod4410_reset_pin_init(void)
{
    GPIO_Config(ZMOD4410_RESET_PORT, ZMOD4410_RESET_PIN, GPIO_CNF_OUT_PP, GPIO_MODE_OUTPUT);
    GPIO_Write_Pin(ZMOD4410_RESET_PORT, ZMOD4410_RESET_PIN, GPIO_PIN_SET);
}

static uint32_t zmod4410_get_warmup_samples(ZMOD4410_OpMode_t operation_mode)
{
    switch (operation_mode)
    {
        case IAQ_2ND_GEN_ULP:
        case RELATIVE_IAQ_ULP:
            return ZMOD4410_WARMUP_SAMPLES_ULP;

        case PUBLIC_BUILDING_AIQ:
            return ZMOD4410_WARMUP_SAMPLES_PBAQ;

        case RELATIVE_IAQ:
            return ZMOD4410_WARMUP_SAMPLES_REL_IAQ;

        case IAQ_2ND_GEN:
        default:
            return ZMOD4410_WARMUP_SAMPLES_IAQ_GEN2;
    }
}

/* -----------------------------------------------------------------------
 * zmod4410_delay_ms — hybrid delay helper (same as AHT20 pattern).
 * ----------------------------------------------------------------------- */
static void zmod4410_delay_ms(uint32_t ms)
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
 * zmod4410_read_register — read one or more bytes from a register.
 * Returns the number of bytes read, or 0 on error.
 * ----------------------------------------------------------------------- */
static uint8_t zmod4410_read_register(I2C_t i2c, uint8_t reg_addr, uint8_t *data, uint8_t len)
{
    if ((data == NULL) || (len == 0U))
    {
        return 0U;
    }

    I2C_Start(i2c);

    /*
     * drv_i2c exposes START/STOP but no repeated-START helper.
     * Use a write transaction to set the register pointer, then a new
     * read transaction. If the final hardware integration proves the
     * sensor needs a strict repeated START, add that primitive in drv_i2c.
     */
    if (!I2C_Transmit_Address(i2c, ZMOD4410_I2C_ADDR, I2C_WRITE))
    {
        I2C_Stop(i2c);
        return 0U;  /* NACK — sensor not responding */
    }

    if (!I2C_Master_Transmit_Data(i2c, &reg_addr, 1U))
    {
        return 0U;  /* Transmission failed */
    }

    I2C_Stop(i2c);

    I2C_Start(i2c);
    if (!I2C_Transmit_Address(i2c, ZMOD4410_I2C_ADDR, I2C_READ))
    {
        I2C_Stop(i2c);
        return 0U;
    }

    if (!I2C_Master_Receive_Data(i2c, data, len))
    {
        return 0U;
    }

    return len;
}

/* -----------------------------------------------------------------------
 * zmod4410_write_register — write one or more bytes to a register.
 * Returns the number of bytes written, or 0 on error.
 * ----------------------------------------------------------------------- */
static uint8_t zmod4410_write_register(I2C_t i2c, uint8_t reg_addr, const uint8_t *data, uint8_t len)
{
    uint8_t buffer[8];

    if ((len + 1U) > sizeof(buffer))
    {
        return 0U;  /* Buffer overflow protection */
    }

    buffer[0] = reg_addr;
    memcpy(&buffer[1], data, len);

    I2C_Start(i2c);
    if (!I2C_Transmit_Address(i2c, ZMOD4410_I2C_ADDR, I2C_WRITE))
    {
        I2C_Stop(i2c);
        return 0U;
    }

    if (!I2C_Master_Transmit_Data(i2c, buffer, (uint8_t)(len + 1U)))
    {
        I2C_Stop(i2c);
        return 0U;
    }

    return len;
}

/* -----------------------------------------------------------------------
 * ZMOD4410_Init — one-time sensor initialization.
 * ----------------------------------------------------------------------- */
ZMOD4410_Status_t ZMOD4410_Init(I2C_t i2c, ZMOD4410_OpMode_t operation_mode)
{
    uint8_t product_id_raw[2];
    uint16_t product_id;

    /* Store operation mode for later use */
    g_operation_mode = operation_mode;
    g_measurement_count = 0U;

    zmod4410_reset_pin_init();
    (void)ZMOD4410_HardReset();

    /* Wait for sensor to stabilize after power-on */
    zmod4410_delay_ms(100U);

    /* Verify sensor is present by reading the official 16-bit PID. */
    if (zmod4410_read_register(i2c, ZMOD4410_REG_PID, product_id_raw, 2U) == 0U)
    {
        return ZMOD4410_ERR_NACK;  /* No response on I2C bus */
    }

    product_id = (uint16_t)(((uint16_t)product_id_raw[0] << 8U) | product_id_raw[1]);

    if (product_id != ZMOD4410_PRODUCT_ID)
    {
        return ZMOD4410_ERR_CONFIG;  /* Wrong product or not initialized */
    }

    /*
     * Full Renesas-compatible initialization also writes mode-specific
     * configuration blocks to 0x40/0x50/0x60/0x68 and uses production data
     * plus a host-side algorithm. Those pieces are intentionally deferred.
     */

    return ZMOD4410_OK;
}

/* -----------------------------------------------------------------------
 * ZMOD4410_HardReset — drive RESET_N low then high.
 * ----------------------------------------------------------------------- */
ZMOD4410_Status_t ZMOD4410_HardReset(void)
{
    zmod4410_reset_pin_init();

    GPIO_Write_Pin(ZMOD4410_RESET_PORT, ZMOD4410_RESET_PIN, GPIO_PIN_RESET);
    zmod4410_delay_ms(2U);
    GPIO_Write_Pin(ZMOD4410_RESET_PORT, ZMOD4410_RESET_PIN, GPIO_PIN_SET);
    zmod4410_delay_ms(10U);

    return ZMOD4410_OK;
}

/* -----------------------------------------------------------------------
 * ZMOD4410_Trigger_Measurement — send trigger command to start measurement.
 * ----------------------------------------------------------------------- */
ZMOD4410_Status_t ZMOD4410_Trigger_Measurement(I2C_t i2c)
{
    uint8_t cmd = ZMOD4410_CMD_TRIGGER;

    if (zmod4410_write_register(i2c, ZMOD4410_REG_CMD, &cmd, 1U) == 0U)
    {
        return ZMOD4410_ERR_NACK;
    }

    return ZMOD4410_OK;
}

/* -----------------------------------------------------------------------
 * ZMOD4410_Measurement_Ready — poll the measurement status.
 * Returns: 1 if ready, 0 if still measuring, <0 on error.
 * ----------------------------------------------------------------------- */
int ZMOD4410_Measurement_Ready(I2C_t i2c)
{
    uint8_t status;

    if (zmod4410_read_register(i2c, ZMOD4410_REG_STATUS, &status, 1U) == 0U)
    {
        return -ZMOD4410_ERR_NACK;
    }

    /* Check if measurement is complete (busy bit = 0) */
    if ((status & ZMOD4410_STATUS_BUSY_BIT) == 0U)
    {
        return 1;  /* Ready */
    }

    return 0;  /* Still measuring */
}

/* -----------------------------------------------------------------------
 * ZMOD4410_Read — read and convert measurement results.
 *
 * The raw data format depends on operation mode. For IAQ 2nd Gen:
 *   Bytes 1-4: IAQ result (index), TVOC (ppb), eCO2 (ppm)
 *   Additional bytes may contain raw sensor data for processing.
 *
 * This is a simplified implementation. Production code should:
 *   - Apply the Renesas algorithm library for accurate conversions
 *   - Handle different operation modes and output formats
 *   - Use calibration coefficients stored in sensor NVM
 * ----------------------------------------------------------------------- */
ZMOD4410_Status_t ZMOD4410_Read(I2C_t i2c, ZMOD4410_Data_t *out)
{
    uint8_t status;
    uint8_t result_data[ZMOD4410_ADC_DATA_LEN];
    uint32_t warmup_samples;

    if (out == NULL)
    {
        return ZMOD4410_ERR_CONFIG;
    }

    /* Read status byte */
    if (zmod4410_read_register(i2c, ZMOD4410_REG_STATUS, &status, 1U) == 0U)
    {
        return ZMOD4410_ERR_NACK;
    }

    /* Check if measurement is still in progress */
    if ((status & ZMOD4410_STATUS_BUSY_BIT) != 0U)
    {
        return ZMOD4410_ERR_BUSY;  /* Not ready yet */
    }

    /* Read raw ADC/result bytes from the official result register. */
    if (zmod4410_read_register(i2c, ZMOD4410_REG_RESULT_DATA, result_data, ZMOD4410_ADC_DATA_LEN) == 0U)
    {
        return ZMOD4410_ERR_NACK;
    }

    memcpy(out->raw_adc, result_data, sizeof(result_data));
    out->raw_len = ZMOD4410_ADC_DATA_LEN;
    g_measurement_count++;
    warmup_samples = zmod4410_get_warmup_samples(g_operation_mode);

    if (g_measurement_count < warmup_samples)
    {
        out->status = ZMOD4410_STATUS_WARM_UP;
    }
    else
    {
        out->status = ZMOD4410_STATUS_VALID;
    }

    /*
     * The official Renesas flow converts raw ADC bytes into IAQ/TVOC/eCO2 with
     * a host-side algorithm. Keep these fields zero until that layer is added.
     */
    out->iaq_index = 0U;
    out->tvoc_ppb = 0U;
    out->tvoc_mg_m3 = 0U;
    out->eco2_ppm = 0U;

    /* Ambient conditions provided by caller via Set_Ambient_Conditions. */
    out->temperature_c = g_ambient_temperature_c;
    out->humidity_pct = g_ambient_humidity_pct;

    return ZMOD4410_OK;
}

/* -----------------------------------------------------------------------
 * ZMOD4410_Set_Ambient_Conditions — provide temperature/humidity for compensation.
 * ----------------------------------------------------------------------- */
ZMOD4410_Status_t ZMOD4410_Set_Ambient_Conditions(I2C_t i2c, float temperature_c, float humidity_pct)
{
    (void)i2c;  /* For future use if sensor accepts these as write registers */

    /* In real implementation, this would:
     * 1. Format temperature and humidity according to sensor spec
     * 2. Write to appropriate sensor registers or
     * 3. Store locally for use in algorithm (if using Renesas library)
     */

    g_ambient_temperature_c = temperature_c;
    g_ambient_humidity_pct = humidity_pct;

    return ZMOD4410_OK;
}
