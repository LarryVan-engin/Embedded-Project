/**
 * @file  safe_reset.h
 * @brief Safe software reset with NVS crash-reason persistence for RA6M5.
 *
 * Purpose
 * -------
 * Provides a crash-safe reset mechanism that:
 *   1. Writes a crash-reason code to the NVS area in Data Flash BEFORE
 *      triggering a Cortex-M33 SYSRESETREQ (so the reason survives power).
 *   2. On the NEXT boot, reads and logs the persisted reason, then erases
 *      the marker so the MCU does NOT reset again in an infinite loop.
 *   3. Periodically checks all task stack canaries (planted by the kernel)
 *      and triggers a safe reset if any canary is corrupted.
 *
 * NVS Block Layout  (last 64-byte Data Flash block at DATA_FLASH_LAST_BLOCK_ADDR)
 * -------------------------------------------------------------------------
 *  Byte  | Content
 *  ------|------------------------------------------------------------------
 *  0..3  | OTA-model magic 0xDE 0xAD 0xBE 0xEF (written by fwupdate)
 *  4..7  | OTA model_len   (big-endian uint32_t)
 *  8..9  | OTA model_crc16 (big-endian uint16_t)
 *  10..11| reserved 0xFF
 *  12..15| Safe-reset magic 0xC0 0xFF 0xEE 0x00  (set by safe_reset_trigger)
 *  16..19| Reset reason code (big-endian uint32_t)
 *  20..23| Cumulative reset count (big-endian uint32_t, never wraps past 255)
 *  24..63| 0xFF padding
 *
 * safe_reset_trigger() modifies only bytes [12..23], preserving bytes [0..11]
 * (OTA metadata) by doing a read-modify-erase-write on the whole 64-byte block.
 *
 * Usage example
 * -------------
 *   // Very first call in hal_entry(), before any task creation:
 *   uint32_t reason = safe_reset_init();
 *   if (reason != SAFE_RESET_REASON_NONE) {
 *       debug_print("[BOOT] Previous crash: reason=0x%08lx\r\n", reason);
 *   }
 *
 *   // Trigger a reset after a fatal error:
 *   safe_reset_trigger(SAFE_RESET_REASON_FAULT);  // does not return
 *
 *   // In a periodic monitor task:
 *   safe_reset_check_stacks();                    // resets on overflow
 */

#ifndef SAFE_RESET_H
#define SAFE_RESET_H

#include <stdint.h>

/* -----------------------------------------------------------------------
 * Reset reason codes (stored in NVS bytes [16..19])
 * ----------------------------------------------------------------------- */
#define SAFE_RESET_REASON_NONE           0x00000000UL /**< Clean boot (no crash)    */
#define SAFE_RESET_REASON_STACK_OVERFLOW 0x00000001UL /**< Task stack canary corrupt */
#define SAFE_RESET_REASON_FAULT          0x00000002UL /**< HardFault / fatal error   */
#define SAFE_RESET_REASON_WATCHDOG       0x00000003UL /**< Application watchdog      */
#define SAFE_RESET_REASON_APP_REQUEST    0x00000004UL /**< Deliberate test reset     */

/* NVS magic written at bytes [12..15] while a crash record is pending */
#define SAFE_RESET_NVS_MAGIC_0  0xC0U
#define SAFE_RESET_NVS_MAGIC_1  0xFFU
#define SAFE_RESET_NVS_MAGIC_2  0xEEU
#define SAFE_RESET_NVS_MAGIC_3  0x00U

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/**
 * @brief Check the NVS block for a crash record from the previous boot.
 *
 * Must be called BEFORE OS_Init() and task creation so that flash P/E
 * operations do not conflict with any running task.
 *
 * If a crash record is found:
 *   - Logs the reason and cumulative reset count via debug_print().
 *   - Erases the crash marker (preserving OTA metadata in bytes [0..11]).
 *
 * @return The reset reason code (SAFE_RESET_REASON_*), or
 *         SAFE_RESET_REASON_NONE if no crash record was present.
 */
uint32_t safe_reset_init(void);

/**
 * @brief Write crash reason to NVS and perform a Cortex-M33 SYSRESETREQ.
 *
 * Performs a read-modify-erase-write on the NVS block to set bytes [12..23]
 * while preserving existing OTA model metadata in bytes [0..11].
 * Then triggers __NVIC_SystemReset via SCB->AIRCR.
 *
 * This function NEVER returns.
 *
 * @param reason  One of the SAFE_RESET_REASON_* constants.
 */
void safe_reset_trigger(uint32_t reason);

/**
 * @brief Check all task stack canaries; trigger a safe reset on overflow.
 *
 * Call from a periodic monitor task (e.g. every 1000 ms).
 * If OS_StackOverflowCheck() reports a corrupted canary, calls
 * safe_reset_trigger(SAFE_RESET_REASON_STACK_OVERFLOW).
 */
void safe_reset_check_stacks(void);

#endif /* SAFE_RESET_H */
