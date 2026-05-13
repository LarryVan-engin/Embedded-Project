#include "test_runner.h"
#include <stddef.h>

uint32_t g_test_pass_count = 0U;
uint32_t g_test_fail_count = 0U;

static test_case_t s_cases[TEST_MAX_CASES];
static uint32_t    s_case_count = 0U;

/* Track whether the current running test has already failed */
static uint8_t s_current_failed = 0U;

void test_register(const char *name, test_fn_t fn)
{
    if (s_case_count < TEST_MAX_CASES)
    {
        s_cases[s_case_count].name = name;
        s_cases[s_case_count].fn   = fn;
        s_case_count++;
    }
}

void test_record_failure(const char *file, int line, const char *expr)
{
    (void)file;
    (void)line;
    (void)expr;
    s_current_failed = 1U;
    /* BUG-16 fix: do NOT increment g_test_fail_count here.
     * g_test_fail_count counts failed test cases, not individual assertions.
     * It is incremented once per test in test_run_all() when s_current_failed=1. */
}

uint32_t test_run_all(void)
{
    g_test_pass_count = 0U;
    g_test_fail_count = 0U;

    for (uint32_t i = 0U; i < s_case_count; i++)
    {
        s_current_failed = 0U;
        s_cases[i].fn();
        if (s_current_failed == 0U)
        {
            g_test_pass_count++;
        }
        else
        {
            g_test_fail_count++;   /* count once per failed test, not per assertion */
        }
    }
    return g_test_fail_count;
}
