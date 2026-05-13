#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdint.h>

/*
 * Minimal test framework for RA6M5 baremetal.
 *
 * Usage:
 *   TEST_ASSERT(condition)   - fails (infinite loop + LED blink) if false
 *   TEST_PASS()              - explicit pass marker
 *   test_run_all()           - runs all registered tests
 */

/* Maximum number of test cases */
#define TEST_MAX_CASES  32U

typedef void (*test_fn_t)(void);

typedef struct
{
    const char  *name;
    test_fn_t    fn;
} test_case_t;

/* Register a test – call before test_run_all() */
void test_register(const char *name, test_fn_t fn);

/* Run all registered tests, return number of failures */
uint32_t test_run_all(void);

/* Internal pass/fail counters (readable after test_run_all) */
extern uint32_t g_test_pass_count;
extern uint32_t g_test_fail_count;

/* Assert macro: on failure, records failure and returns from current test */
#define TEST_ASSERT(cond)                                    \
    do {                                                     \
        if (!(cond)) {                                       \
            test_record_failure(__FILE__, __LINE__, #cond);  \
            return;                                          \
        }                                                    \
    } while (0)

#define TEST_ASSERT_EQUAL(expected, actual)                  \
    TEST_ASSERT((expected) == (actual))

#define TEST_PASS()  /* nothing – reaching end of test is a pass */

/* Internal: called by TEST_ASSERT macro */
void test_record_failure(const char *file, int line, const char *expr);

#endif /* TEST_RUNNER_H */
