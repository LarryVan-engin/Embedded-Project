#include "test_iaq_zmod4410_real.h"

#include "bsp_zmod4410.h"
#include "debug_print.h"
#include "iaq_predictor.h"
#include "kernel.h"
#include <stdbool.h>
#include <stdint.h>

#define ZMOD4410_REAL_TEST_DELAY_MS 3000U

static void print_fixed_2dp(const char *tag, float value)
{
    int32_t scaled = (int32_t)(value * 100.0f);
    int32_t integer_part = scaled / 100;
    int32_t frac_abs = scaled % 100;

    if (frac_abs < 0)
    {
        frac_abs = -frac_abs;
    }

    debug_print("%s=%d.%d%d", tag, (int)integer_part, (int)(frac_abs / 10), (int)(frac_abs % 10));
}

void test_iaq_zmod4410_real_manual_run(I2C_t i2c, uint8_t pclkb_mhz, uint32_t sample_count)
{
    bool model_ok;
    ZMOD4410_Status_t status;

    if (sample_count == 0U)
    {
        debug_print("[ZMOD4410_REAL_AI_TEST] sample_count=0, skip.\r\n");
        return;
    }

    debug_print("\r\n[ZMOD4410_REAL_AI_TEST] Manual run start (%lu samples)\r\n", (unsigned long) sample_count);

    I2C_Init(i2c, pclkb_mhz, I2C_SPEED_FAST);

    status = ZMOD4410_Init(i2c, IAQ_2ND_GEN);
    if (status != ZMOD4410_OK)
    {
        debug_print("[ZMOD4410_REAL_AI_TEST] ZMOD4410_Init failed: %d\r\n", (int) status);
        return;
    }

    model_ok = IAQ_Init();
    if (!model_ok)
    {
        debug_print("[ZMOD4410_REAL_AI_TEST] IAQ_Init failed.\r\n");
        return;
    }

    for (uint32_t index = 0U; index < sample_count; index++)
    {
        ZMOD4410_Data_t data;
        uint16_t tvoc_proxy_ppb;
        float predicted_iaq;

        status = ZMOD4410_Trigger_Measurement(i2c);
        if (status != ZMOD4410_OK)
        {
            debug_print("[ZMOD4410_REAL_AI_TEST] Trigger failed at sample %lu: %d\r\n",
                        (unsigned long) (index + 1U),
                        (int) status);
            continue;
        }

        OS_Task_Delay(ZMOD4410_REAL_TEST_DELAY_MS);

        status = ZMOD4410_Read(i2c, &data);
        if (status != ZMOD4410_OK)
        {
            debug_print("[ZMOD4410_REAL_AI_TEST] Read failed at sample %lu: %d\r\n",
                        (unsigned long) (index + 1U),
                        (int) status);
            continue;
        }

        tvoc_proxy_ppb = (uint16_t)(((uint16_t)data.raw_adc[0] << 8U) | (uint16_t)data.raw_adc[1]);
        predicted_iaq = IAQ_Predict((float) tvoc_proxy_ppb);

        debug_print("[ZMOD4410_REAL_AI_TEST] #%lu status=%u raw_len=%u raw0=0x%02X raw1=0x%02X tvoc_proxy=%u ",
                    (unsigned long) (index + 1U),
                    (unsigned int) data.status,
                    (unsigned int) data.raw_len,
                    (unsigned int) data.raw_adc[0],
                    (unsigned int) data.raw_adc[1],
                    (unsigned int) tvoc_proxy_ppb);
        print_fixed_2dp("predict_iaq", predicted_iaq);
        debug_print("\r\n");
    }

    debug_print("[ZMOD4410_REAL_AI_TEST] Manual run done.\r\n");
}
