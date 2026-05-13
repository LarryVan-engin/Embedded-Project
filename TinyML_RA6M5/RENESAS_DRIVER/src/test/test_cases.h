/**
 * @file    test_cases.h
 * @brief   Test case declarations.
 *
 * Registered tests:
 *   - test_iaq: TensorFlow Lite IAQ model inference tests
 *   - test_rtos: Bare-metal RTOS kernel tests
 */

#ifndef TEST_IAQ_H
#define TEST_IAQ_H
void test_iaq_register(void);
#endif

#ifndef TEST_RTOS_H
#define TEST_RTOS_H
void test_rtos_register(void);
#endif
