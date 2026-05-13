/*
 * hal_entry.c  --  Application entry for TESTING_2 bare-metal RA6M5 project.
 *
 * Board: CK-RA6M5 (R7FA6M5BH3CFC, LQFP176)
 *
 * LED assignments (CK-RA6M5):
 *   LED1 Red   -- P610  (Port 6, Pin 10)
 *   LED2 Green -- P603  (Port 6, Pin 3)
 *   LED3 Green -- P609  (Port 6, Pin 9)
 *
 * LEDs on CK-RA6M5 are active-HIGH (driving pin HIGH = LED ON).
 * UART: SCI1 via J-Link OB VCOM (P708 RX / P709 TX) @ 115200 baud
 *
 * This file:
 *   1. Runs safe_reset_init() to check for a persisted crash record and
 *      clear it (prevents infinite reset loops).
 *   2. On a clean first boot (SAFE_RESET_SELF_TEST enabled), triggers a
 *      deliberate reset to validate the NVS crash-record mechanism.  On
 *      the second boot the crash record is found, logged, and cleared,
 *      then execution continues normally.
 *   3. Configures LED1 as GPIO push-pull output.
 *   4. Runs the bare-metal driver test suite (src/test/).
 *   5. Blinks LED1 to report results:
 *        Slow blink (500 ms period) -- all tests passed
 *        Fast blink (100 ms period) -- one or more tests failed
 */

#include "hal_entry.h"
#include "GPIO.h"
#include "utils.h"
#include "debug_print.h"
#include "safe_reset.h"
#include "test/test_runner.h"
#include "test/test_cases.h"
#include <sys/types.h>

/* -----------------------------------------------------------------
 * safe_reset self-test gate.
 * Set SAFE_RESET_SELF_TEST to 1 to enable the deliberate first-boot
 * reset that validates the NVS crash-record mechanism.  Disable for
 * normal production operation.
 * ----------------------------------------------------------------- */
#define SAFE_RESET_SELF_TEST  1

/* -----------------------------------------------------------------
 * LED pin definitions  (Port 6, Pin 10 = P610 = LED1 Red on CK-RA6M5)
 * ----------------------------------------------------------------- */
#define LED1_PORT   GPIO_PORT6
#define LED1_PIN    10U

/* Ghi đè hàm _sbrk để Linker không đòi symbol 'end' từ file .ld nữa */
#ifdef __cplusplus
extern "C" {
#endif
caddr_t _sbrk(int incr) {
    (void)incr;
    // Trả về -1 để báo hết bộ nhớ, chặn đứng mọi nỗ lực gọi malloc() ngầm
    return (caddr_t)-1; 
}
#ifdef __cplusplus
}
#endif

/* -----------------------------------------------------------------
 * hal_entry  --  called from main()
 * ----------------------------------------------------------------- */
void hal_entry(void)
{
    /*
     * --- Safe-reset init: MUST be the very first operation ---
     *
     * Reads the NVS crash record (Data Flash last block, bytes [12..15]).
     * If a record exists (from a previous safe_reset_trigger() call):
     *   - Logs the reason and cumulative reset count via debug_print().
     *   - Erases only the crash marker, preserving OTA model metadata.
     *   - Returns the reason code so the app can react.
     * This prevents an infinite reset loop: the marker is cleared before
     * any code that might fault again is reached.
     */
    uint32_t crash_reason = safe_reset_init();

    /* Configure LED1 (P610) as GPIO push-pull output */
    GPIO_Config(LED1_PORT, LED1_PIN, GPIO_CNF_OUT_PP, GPIO_MODE_OUTPUT);

    /* CK LEDs are active-high: LOW = OFF, HIGH = ON */
    GPIO_Write_Pin(LED1_PORT, LED1_PIN, GPIO_PIN_RESET);

#if SAFE_RESET_SELF_TEST
    if (crash_reason == SAFE_RESET_REASON_NONE)
    {
        /*
         * First boot (no crash marker found):
         * Trigger a deliberate reset to write a test crash record to NVS.
         * On the next boot, safe_reset_init() will find and clear the record,
         * proving the NVS persistence + recovery path works end-to-end.
         *
         * safe_reset_trigger() never returns — the MCU resets immediately.
         */
        debug_print("[SELF_TEST] safe_reset: triggering deliberate reset to NVS...");
        safe_reset_trigger(SAFE_RESET_REASON_APP_REQUEST);
    }
    else
    {
        /*
         * Second (or later) boot: crash record was found and cleared by
         * safe_reset_init() above.  Log the outcome and continue normally.
         * No further reset is triggered, proving the mechanism does NOT
         * loop once the marker has been consumed.
         */
        debug_print("[SELF_TEST] safe_reset OK: recovered from reason=0x%08lx, continuing.\r\n",
                    (unsigned long)crash_reason);
    }
#else
    (void)crash_reason;  /* suppress unused-variable warning in production builds */
#endif /* SAFE_RESET_SELF_TEST */

    /* Run driver test suite (only IAQ and RTOS tests enabled) */
    test_iaq_register();
    test_rtos_register();

    uint32_t failures = test_run_all();

    /* Indicate result via LED blink rate */
    uint32_t half_period_ms = (failures == 0U) ? 500U : 100U;

    while (1)
    {
        GPIO_Write_Pin(LED1_PORT, LED1_PIN, GPIO_PIN_SET);   /* LED ON  */
        delay_ms(half_period_ms);
        GPIO_Write_Pin(LED1_PORT, LED1_PIN, GPIO_PIN_RESET); /* LED OFF */
        delay_ms(half_period_ms);
    }
}
