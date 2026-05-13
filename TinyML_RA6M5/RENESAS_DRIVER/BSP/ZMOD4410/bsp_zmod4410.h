/**
 * @file    bsp_zmod4410.h
 * @brief   Lightweight BSP driver for ZMOD4410 gas sensor (I2C).
 *
 * Sensor: Renesas ZMOD4410
 * Interface: I2C, 7-bit address 0x32, up to 400 kHz
 * Requires: drv_i2c driver (I2C_Init must be called before ZMOD4410_Init)
 *
 * Notes:
 *   - The ZMOD4410 does not expose final IAQ/TVOC/eCO2 values directly.
 *   - Renesas' reference stack configures the sensor with mode-specific data
 *     blocks and converts raw ADC bytes with a host-side algorithm library.
 *   - This BSP currently provides: PID check, measurement trigger, status
 *     polling, raw ADC readback, and ambient-condition storage.
 *   - Final IAQ/TVOC/eCO2 calculation is intentionally left for a later
 *     integration step with the official Renesas data set / algorithm.
 *
 * Typical usage:
 *
 *   I2C_Init(I2C0, 8U, I2C_SPEED_FAST);   // 8 MHz PCLKB, 400 kHz
 *   ZMOD4410_Init(I2C0, IAQ_2ND_GEN);
 *   ZMOD4410_Trigger_Measurement(I2C0);
 *
 *   // Wait for 3 seconds (IAQ 2nd Gen sample rate)
 *   OS_Task_Delay(3000);
 *
 *   ZMOD4410_Data_t data;
 *   if (ZMOD4410_Read(I2C0, &data) == ZMOD4410_OK) {
 *       debug_print("raw[0]=0x%02X raw[1]=0x%02X len=%u\r\n",
 *                   data.raw_adc[0],
 *                   data.raw_adc[1],
 *                   (unsigned)data.raw_len);
 *   }
 *
 * Hardware connections (CK-RA6M5, onboard ZMOD4410):
 *   SCL   → P400
 *   SDA   → P401
 *   INT   → P402 (optional, not used in basic polling mode)
 *   RES_N → P307 (active-low hard reset control)
 *   External pull-up resistors (4.7 kΩ to 3.3 V) required on SCL and SDA.
 */

#ifndef BSP_ZMOD4410_H
#define BSP_ZMOD4410_H

#include "drv_i2c.h"
#include <stdint.h>

/* -----------------------------------------------------------------------
 * ZMOD4410 fixed I2C address (7-bit, not shifted)
 * ----------------------------------------------------------------------- */
#define ZMOD4410_I2C_ADDR   0x32U
#define ZMOD4410_PRODUCT_ID 0x2310U
#define ZMOD4410_ADC_DATA_LEN 32U

/* -----------------------------------------------------------------------
 * Operation modes
 * ----------------------------------------------------------------------- */
typedef enum {
    IAQ_2ND_GEN = 0,              /* Absolute IAQ, TVOC, eCO2 (recommended)  */
    IAQ_2ND_GEN_ULP = 1,           /* Ultra-low power variant (90 sec sample) */
    PUBLIC_BUILDING_AIQ = 2,       /* PBAQ standards (5 sec sample)           */
    RELATIVE_IAQ = 3,              /* Relative change detection (24h decay)   */
    RELATIVE_IAQ_ULP = 4           /* Relative IAQ ultra-low power            */
} ZMOD4410_OpMode_t;

/* -----------------------------------------------------------------------
 * Return status codes
 * ----------------------------------------------------------------------- */
typedef enum {
    ZMOD4410_OK          = 0,     /* Measurement complete and valid         */
    ZMOD4410_ERR_NACK    = 1,     /* Sensor did not ACK — check wiring      */
    ZMOD4410_ERR_BUSY    = 2,     /* Sensor still measuring — retry         */
    ZMOD4410_ERR_TIMEOUT = 3,     /* I2C bus timeout                        */
    ZMOD4410_ERR_CRC     = 4,     /* CRC check failed                       */
    ZMOD4410_ERR_CONFIG  = 5      /* Unsupported/incomplete configuration   */
} ZMOD4410_Status_t;

/* -----------------------------------------------------------------------
 * Sensor status flags
 * ----------------------------------------------------------------------- */
typedef enum {
    ZMOD4410_STATUS_WARM_UP  = 0,  /* Raw data read OK but algorithm is stabilizing */
    ZMOD4410_STATUS_VALID    = 1,  /* Raw measurement completed successfully        */
    ZMOD4410_STATUS_DAMAGED  = 2   /* Device error detected                         */
} ZMOD4410_Sensor_Status_t;

/* -----------------------------------------------------------------------
 * Measurement result — IAQ 2nd Generation mode
 * ----------------------------------------------------------------------- */
typedef struct {
    uint8_t iaq_index;               /* 0 until official IAQ algorithm is integrated       */
    uint16_t tvoc_ppb;               /* 0 until official TVOC algorithm is integrated      */
    uint16_t tvoc_mg_m3;             /* 0 until official TVOC algorithm is integrated      */
    uint16_t eco2_ppm;               /* 0 until official eCO2 algorithm is integrated      */
    float temperature_c;             /* Ambient temperature cached for future algorithm     */
    float humidity_pct;              /* Ambient humidity cached for future algorithm        */
    ZMOD4410_Sensor_Status_t status; /* Raw measurement status                              */
    uint8_t raw_len;                 /* Number of valid bytes in raw_adc                    */
    uint8_t raw_adc[ZMOD4410_ADC_DATA_LEN]; /* Raw ADC/result bytes from sensor         */
} ZMOD4410_Data_t;

/* -----------------------------------------------------------------------
 * Configuration structure
 * ----------------------------------------------------------------------- */
typedef struct {
    ZMOD4410_OpMode_t operation_mode;  /* Selected operation mode            */
    float temperature_c;               /* Ambient temperature (for comp.)    */
    float humidity_pct;                /* Ambient humidity (for comp.)       */
} ZMOD4410_Config_t;

/* -----------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */

/**
 * ZMOD4410_Init — one-time sensor initialization.
 *
 * Must be called once after I2C_Init and after all power-on delays.
 * Current implementation verifies the sensor PID and stores the requested
 * operation mode, but does not yet load the Renesas mode-specific data sets.
 *
 * @param i2c              I2C channel (I2C0, I2C1, or I2C2)
 * @param operation_mode   Selected operation mode (e.g., IAQ_2ND_GEN)
 * @return                 ZMOD4410_OK on success; error code on failure
 */
ZMOD4410_Status_t ZMOD4410_Init(I2C_t i2c, ZMOD4410_OpMode_t operation_mode);

/**
 * ZMOD4410_HardReset — toggle RESET_N (P307) low/high to reset sensor.
 *
 * @return      ZMOD4410_OK on success
 */
ZMOD4410_Status_t ZMOD4410_HardReset(void);

/**
 * ZMOD4410_Trigger_Measurement — start a sensor measurement.
 *
 * Non-blocking: sends the command byte used to start a measurement sequence.
 * Caller must wait for the appropriate sample time before reading.
 *   - IAQ 2nd Gen: 3 seconds
 *   - IAQ 2nd Gen ULP: 90 seconds
 *   - PBAQ: 5 seconds
 *   - Relative IAQ: 3 seconds
 *
 * @param i2c   I2C channel (must match the channel used in ZMOD4410_Init)
 * @return      ZMOD4410_OK on success; error code on failure
 */
ZMOD4410_Status_t ZMOD4410_Trigger_Measurement(I2C_t i2c);

/**
 * ZMOD4410_Read — read measurement results from the sensor.
 *
 * Reads the raw sensor result bytes.
 * Must be called after sufficient time has elapsed since triggering.
 * Final IAQ/TVOC/eCO2 fields remain zero until the official Renesas
 * host-side algorithm layer is integrated.
 *
 * @param i2c   I2C channel (must match the channel used in ZMOD4410_Init)
 * @param out   Pointer to ZMOD4410_Data_t; filled on ZMOD4410_OK return
 * @return      ZMOD4410_OK on success; error code on failure
 */
ZMOD4410_Status_t ZMOD4410_Read(I2C_t i2c, ZMOD4410_Data_t *out);

/**
 * ZMOD4410_Measurement_Ready — check if measurement is complete.
 *
 * Polls the INT pin status or measurement state without reading data.
 * INT pin is HIGH during measurement, LOW when complete (falling edge).
 *
 * @param i2c   I2C channel
 * @return      1 if measurement complete, 0 if still measuring, <0 on error
 */
int ZMOD4410_Measurement_Ready(I2C_t i2c);

/**
 * ZMOD4410_Set_Ambient_Conditions — provide temperature/humidity for compensation.
 *
 * Optional but recommended for future IAQ 2nd Gen algorithm integration.
 * The current BSP stores these values locally; it does not write them into
 * the sensor over I2C.
 *
 * @param i2c               I2C channel
 * @param temperature_c     Ambient temperature in °C
 * @param humidity_pct      Relative humidity in %
 * @return                  ZMOD4410_OK on success
 */
ZMOD4410_Status_t ZMOD4410_Set_Ambient_Conditions(I2C_t i2c, float temperature_c, float humidity_pct);

#endif /* BSP_ZMOD4410_H */
