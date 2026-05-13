/**
 * @file  test_iaq.c
 * @brief RTOS task: IAQ inference test with real ZMOD4410 sensor data.
 */

#include "test_iaq.h"
#include "debug_print.h"
#include "drv_i2c.h"
#include "drv_usb.h"
#include "iaq_predictor.h"
#include "kernel.h"
#include "server_comm.h"
#include "bsp_zmod4410.h"
#include "test/test_runner.h"
#include <stdbool.h>
#include <stdint.h>

/* --- Configuration --- */
#define IAQ_STARTUP_DELAY_MS 3000U
#define IAQ_STARTUP_POLL_MS    10U
#define ZMOD_SAMPLE_DELAY_MS 3000U  /* ZMOD4410 sample rate: 3 seconds for IAQ_2ND_GEN */
#define CYCLE_DELAY_MS 5000U        /* 5-second task cycle */
#define ZMOD_I2C_CH I2C0            /* CK-RA6M5 onboard ZMOD4410: SCL=P400, SDA=P401 */
#define PCLKB_MHZ 50U               /* Production clock tree: PCLKB = 50 MHz */

static OS_TCB_t s_iaq_test_tcb;

/* =========================================================================
 * Task body — reads real ZMOD4410 sensor data
 * ========================================================================= */
static void task_iaq_test(void *arg) {
  uint32_t startup_wait_ms = 0U;
  ZMOD4410_Status_t zmod_status;

  (void)arg;

  /* Keep the heavy TFLM init out of the default-address USB enumeration window. */
  while ((startup_wait_ms < IAQ_STARTUP_DELAY_MS) && (USB_Dev_IsConfigured() == 0U)) {
    OS_Task_Delay(IAQ_STARTUP_POLL_MS);
    startup_wait_ms += IAQ_STARTUP_POLL_MS;
  }

  debug_print("\r\n[IAQ Task] Starting I2C and ZMOD4410 init...\r\n");

  /* Initialize I2C0 @ 400 kHz for onboard ZMOD4410 (P400/P401). */
  I2C_Init(ZMOD_I2C_CH, PCLKB_MHZ, I2C_SPEED_FAST);
  debug_print("[IAQ Task] I2C0 initialized (P400/P401)\r\n");

  /* Initialize ZMOD4410 sensor */
  zmod_status = ZMOD4410_Init(ZMOD_I2C_CH, IAQ_2ND_GEN);
  if (zmod_status != ZMOD4410_OK) {
    debug_print("FATAL: ZMOD4410_Init() failed with status=%u\r\n", (unsigned)zmod_status);
    for (;;) {
      OS_Task_Delay(5000U);
    }
  }
  debug_print("[IAQ Task] ZMOD4410 initialized\r\n");

  /* Initialize TFLite interpreter */
  bool init_ok = IAQ_Init();
  if (!init_ok) {
    debug_print("FATAL: IAQ_Init() failed. Task halted.\r\n");
    for (;;) {
      OS_Task_Delay(5000U);
    }
  }

  debug_print("[IAQ Task] Init OK. Starting ZMOD4410 sensor loop...\r\n");

  for (;;) {
    ZMOD4410_Data_t zmod_data;
    uint16_t tvoc_ppb;
    float forecast;

    /* Trigger measurement */
    zmod_status = ZMOD4410_Trigger_Measurement(ZMOD_I2C_CH);
    if (zmod_status != ZMOD4410_OK) {
      debug_print("[IAQ Task] ZMOD trigger failed: %u\r\n", (unsigned)zmod_status);
      OS_Task_Delay(CYCLE_DELAY_MS);
      continue;
    }

    /* Wait for sensor to complete measurement */
    OS_Task_Delay(ZMOD_SAMPLE_DELAY_MS);

    /* Read sensor data */
    zmod_status = ZMOD4410_Read(ZMOD_I2C_CH, &zmod_data);
    if (zmod_status != ZMOD4410_OK) {
      debug_print("[IAQ Task] ZMOD read failed: %u\r\n", (unsigned)zmod_status);
      OS_Task_Delay(CYCLE_DELAY_MS);
      continue;
    }

    /* Extract TVOC proxy from raw ADC values */
    tvoc_ppb = (uint16_t)(((uint16_t)zmod_data.raw_adc[0] << 8U) | (uint16_t)zmod_data.raw_adc[1]);

    /* Run AI prediction */
    forecast = IAQ_Predict((float)tvoc_ppb);

    /* Print results with integer arithmetic to avoid debug_print issues */
    int32_t tvoc_10 = (int32_t)(tvoc_ppb);
    int32_t predict_100 = (int32_t)(forecast * 100.0f);

    int t_int = (int)(tvoc_10 / 10);
    int t_frac = (int)(tvoc_10 % 10);

    int p_int = (int)(predict_100 / 100);
    int p_frac1 = (int)((predict_100 % 100) / 10);
    int p_frac2 = (int)(predict_100 % 10);

    debug_print("[IAQ] TVOC=%d.%dppb | Predict=%d.%d%d\r\n",
                t_int, t_frac, p_int, p_frac1, p_frac2);

    server_comm_publish_raw((float)tvoc_ppb, 0.0f);
    server_comm_publish_predict(forecast);

    /* Wait before next cycle */
    OS_Task_Delay(CYCLE_DELAY_MS);
  }
}

/**
 * @brief Placeholder test case for test framework
 */
static void test_iaq_case(void) {
  /* Actual testing happens via task_iaq_test */
  debug_print("[TEST] IAQ inference test registered\r\n");
}

/* =========================================================================
 * Public API
 * ========================================================================= */
void test_iaq_inference_init(void) {
  (void)OS_Task_Create(&s_iaq_test_tcb, task_iaq_test, (void *)0, 6U,
                       "iaq_test");
}

/**
 * @brief Register IAQ test for test framework
 */
void test_iaq_register(void) {
  test_register("IAQ", test_iaq_case);
  test_iaq_inference_init();
}