#ifndef TEST_IAQ_ZMOD4410_REAL_H
#define TEST_IAQ_ZMOD4410_REAL_H

#include "drv_i2c.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Manual-only AI test with real ZMOD4410 sensor input.
 *
 * This function is intentionally NOT auto-registered in test_cases/hal_entry.
 * Call it manually from a temporary debug flow when needed.
 *
 * @param i2c          I2C channel connected to ZMOD4410.
 * @param pclkb_mhz    PCLKB frequency in MHz for I2C_Init (for example, 8U).
 * @param sample_count Number of measurement cycles to run.
 */
void test_iaq_zmod4410_real_manual_run(I2C_t i2c, uint8_t pclkb_mhz, uint32_t sample_count);

#ifdef __cplusplus
}
#endif

#endif /* TEST_IAQ_ZMOD4410_REAL_H */
