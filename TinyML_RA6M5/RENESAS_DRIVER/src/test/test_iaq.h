#ifndef TEST_IAQ_H
#define TEST_IAQ_H

/**
 * @file  test_iaq.h
 * @brief RTOS test task for IAQ future-forecast inference.
 *
 * Test strategy:
 *   1. Initialize TFLite interpreter (IAQ_Init).
 *   2. Run 20 synthetic sensor samples through the model.
 *   3. For each sample, print TVOC, AI forecast, and UBA reference IAQ.
 *   4. Report PASS if all forecasts are in range [1.0, 5.0] and no
 *      Invoke() errors occurred. A WARN is printed if the average absolute
 *      error between forecast and reference exceeds 0.5 IAQ points (this is
 *      acceptable for a future-state predictor; exact match is not expected).
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create and register the IAQ inference test RTOS task.
 *        Call after OS_Init(), before OS_Start().
 */
void test_iaq_inference_init(void);

#ifdef __cplusplus
}
#endif

#endif /* TEST_IAQ_H */
