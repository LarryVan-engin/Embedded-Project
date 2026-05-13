# FW_TestFramework

Tags: #done #firmware #system

Bare-metal test framework for RA6M5. Files: `src/test/test_runner.h`, `src/test/test_runner.c`, `src/test/test_cases.h`. Entry point: `hal_entry.c` (calls all `test_xxx_register()` functions, then `test_run_all()`).

---

## Architecture

```
hal_entry()
  ├── test_gpio_register()   → registers 3 test cases
  ├── test_rwp_register()    → registers 2 test cases
  ├── test_uart_register()   → registers 3 test cases
  ├── test_i2c_register()    → registers 3 test cases
  └── test_run_all()         → runs all 11, returns fail count
        └── LED blink: slow (500ms) = pass, fast (100ms) = fail
```

---

## API

```c
void     test_register(const char *name, test_fn_t fn);
uint32_t test_run_all(void);

/* Assert macros (use inside test functions): */
TEST_ASSERT(cond)
TEST_ASSERT_EQUAL(expected, actual)
```

On `TEST_ASSERT` failure: records failure via `test_record_failure()`, then `return`s from the test function (does not abort the entire run).

---

## BUG-16 Fix

`g_test_fail_count` was incremented inside `test_record_failure()`, counting per-assertion. A test with 3 failing assertions would add 3 to the count, making the LED blink count meaningless.

Fix: `g_test_fail_count++` moved to `test_run_all()` in the `else` branch. Now counts **failed test cases** (0–N), not total assertion failures.

---

## Test Files

| File | Tests | Hardware Required |
|------|-------|------------------|
| `test_gpio.c` | 3 | None (index arithmetic) |
| `test_rwp.c` | 2 | Yes (writes PRCR) |
| `test_uart.c` | 3 | Yes (inits SCI7) |
| `test_i2c.c` | 3 | Yes (inits RIIC0) |

---

## Result Reporting

`test_run_all()` returns `g_test_fail_count`. `hal_entry()` blinks LED1 (P006):
- 500 ms half-period: all tests passed
- 100 ms half-period: one or more tests failed

`g_test_pass_count` and `g_test_fail_count` are also readable from a debugger.
