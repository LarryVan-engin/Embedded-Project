/**
 * @file  safe_reset.c
 * @brief Safe software reset with NVS crash-reason persistence (RA6M5).
 *
 * See safe_reset.h for design overview and NVS block layout.
 */

#include "safe_reset.h"
#include "drv_flash_hp.h"
#include "debug_print.h"
#include "kernel.h"          /* OS_StackOverflowCheck() */

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
 * ARM Cortex-M33 System Control Block — Application Interrupt and Reset
 * Control Register (AIRCR) at 0xE000ED0C.
 *
 * Writing VECTKEY = 0x05FA and SYSRESETREQ (bit 2) triggers a system reset.
 * ----------------------------------------------------------------------- */
#define SCB_AIRCR  (*(volatile uint32_t *)0xE000ED0CUL)
#define AIRCR_VECTKEY_SYSREESET  ((0x05FAUL << 16U) | (1UL << 2U))

/* -----------------------------------------------------------------------
 * NVS byte offsets within the 64-byte block
 * ----------------------------------------------------------------------- */
#define NVS_OFF_SAFE_MAGIC_0  12U
#define NVS_OFF_SAFE_MAGIC_1  13U
#define NVS_OFF_SAFE_MAGIC_2  14U
#define NVS_OFF_SAFE_MAGIC_3  15U
#define NVS_OFF_REASON_0      16U
#define NVS_OFF_REASON_1      17U
#define NVS_OFF_REASON_2      18U
#define NVS_OFF_REASON_3      19U
#define NVS_OFF_COUNT_0       20U
#define NVS_OFF_COUNT_1       21U
#define NVS_OFF_COUNT_2       22U
#define NVS_OFF_COUNT_3       23U

/* Erased value of a Data Flash byte */
#define NVS_BLANK_BYTE  0xFFU

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

/** Trigger Cortex-M33 SYSRESETREQ — never returns. */
static void hw_system_reset(void)
{
    __asm volatile("dsb 0xF" ::: "memory");
    SCB_AIRCR = AIRCR_VECTKEY_SYSREESET;
    __asm volatile("dsb 0xF" ::: "memory");
    for (;;)
    {
        __asm volatile("nop");
    }
}

/** Read four bytes at buf[off..off+3] as a big-endian uint32_t. */
static uint32_t be32_read(const uint8_t *buf, uint32_t off)
{
    return ((uint32_t)buf[off + 0U] << 24U) |
           ((uint32_t)buf[off + 1U] << 16U) |
           ((uint32_t)buf[off + 2U] <<  8U) |
           ((uint32_t)buf[off + 3U]);
}

/** Write a big-endian uint32_t into buf[off..off+3]. */
static void be32_write(uint8_t *buf, uint32_t off, uint32_t value)
{
    buf[off + 0U] = (uint8_t)((value >> 24U) & 0xFFU);
    buf[off + 1U] = (uint8_t)((value >> 16U) & 0xFFU);
    buf[off + 2U] = (uint8_t)((value >>  8U) & 0xFFU);
    buf[off + 3U] = (uint8_t)( value         & 0xFFU);
}

/** Return 1 if bytes [12..15] of buf contain the safe-reset magic. */
static uint8_t nvs_has_crash_magic(const uint8_t *buf)
{
    return (buf[NVS_OFF_SAFE_MAGIC_0] == SAFE_RESET_NVS_MAGIC_0 &&
            buf[NVS_OFF_SAFE_MAGIC_1] == SAFE_RESET_NVS_MAGIC_1 &&
            buf[NVS_OFF_SAFE_MAGIC_2] == SAFE_RESET_NVS_MAGIC_2 &&
            buf[NVS_OFF_SAFE_MAGIC_3] == SAFE_RESET_NVS_MAGIC_3) ? 1U : 0U;
}

/**
 * @brief Read the last 64-byte NVS block into a local buffer.
 *
 * Data Flash is memory-mapped in read mode; a simple byte-by-byte copy is
 * sufficient — no P/E entry required.
 */
static void nvs_read_block(uint8_t *buf)
{
    const volatile uint8_t *p_nvs =
        (const volatile uint8_t *)(uintptr_t)DATA_FLASH_LAST_BLOCK_ADDR;
    uint32_t i;

    for (i = 0U; i < DATA_FLASH_BLOCK_SIZE; i++)
    {
        buf[i] = p_nvs[i];
    }
}

/**
 * @brief Erase the last NVS block and write back buf[0..63].
 *
 * P/E mode must already be entered by the caller.
 *
 * @return FLASH_HP_OK on success, or flash error code.
 */
static flash_hp_status_t nvs_erase_and_write(const uint8_t *buf)
{
    flash_hp_status_t st;

    st = flash_hp_erase(DATA_FLASH_LAST_BLOCK_ADDR, 1U);
    if (st != FLASH_HP_OK)
    {
        return st;
    }

    st = flash_hp_write(DATA_FLASH_LAST_BLOCK_ADDR, buf, (uint32_t)DATA_FLASH_BLOCK_SIZE);
    return st;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

uint32_t safe_reset_init(void)
{
    uint8_t buf[DATA_FLASH_BLOCK_SIZE];
    uint32_t reason;
    uint32_t count;
    flash_hp_status_t st;

    /* Read the NVS block (memory-mapped, no P/E needed). */
    nvs_read_block(buf);

    /* Check for a crash marker left by a previous safe_reset_trigger(). */
    if (nvs_has_crash_magic(buf) == 0U)
    {
        return SAFE_RESET_REASON_NONE;   /* No crash record — clean boot. */
    }

    reason = be32_read(buf, NVS_OFF_REASON_0);
    count  = be32_read(buf, NVS_OFF_COUNT_0);

    debug_print("[SAFE_RESET] Previous crash detected: reason=0x%08lx  count=%lu\r\n",
                (unsigned long)reason, (unsigned long)count);

    /* Erase the crash marker (preserve OTA metadata in bytes [0..11]). */
    buf[NVS_OFF_SAFE_MAGIC_0] = NVS_BLANK_BYTE;
    buf[NVS_OFF_SAFE_MAGIC_1] = NVS_BLANK_BYTE;
    buf[NVS_OFF_SAFE_MAGIC_2] = NVS_BLANK_BYTE;
    buf[NVS_OFF_SAFE_MAGIC_3] = NVS_BLANK_BYTE;
    be32_write(buf, NVS_OFF_REASON_0, 0xFFFFFFFFUL);  /* blank */
    be32_write(buf, NVS_OFF_COUNT_0,  count);          /* keep count for reference */

    st = flash_hp_init();
    if (st == FLASH_HP_OK)
    {
        (void)nvs_erase_and_write(buf);
        flash_hp_exit();
    }
    else
    {
        debug_print("[SAFE_RESET] flash init failed (%d) — marker NOT cleared\r\n",
                    (int)st);
    }

    return reason;
}

void safe_reset_trigger(uint32_t reason)
{
    uint8_t buf[DATA_FLASH_BLOCK_SIZE];
    uint32_t count;
    flash_hp_status_t st;

    /* --- Read existing NVS block (memory-mapped read, no P/E needed). --- */
    nvs_read_block(buf);

    /* --- Update crash fields in the local buffer. --- */

    /* Set the safe-reset magic at bytes [12..15]. */
    buf[NVS_OFF_SAFE_MAGIC_0] = SAFE_RESET_NVS_MAGIC_0;
    buf[NVS_OFF_SAFE_MAGIC_1] = SAFE_RESET_NVS_MAGIC_1;
    buf[NVS_OFF_SAFE_MAGIC_2] = SAFE_RESET_NVS_MAGIC_2;
    buf[NVS_OFF_SAFE_MAGIC_3] = SAFE_RESET_NVS_MAGIC_3;

    /* Store the reason at bytes [16..19] (big-endian). */
    be32_write(buf, NVS_OFF_REASON_0, reason);

    /* Increment the cumulative reset count at bytes [20..23].
     * A blank (0xFFFFFFFF) count is treated as 0 before incrementing. */
    count = be32_read(buf, NVS_OFF_COUNT_0);
    if (count == 0xFFFFFFFFUL)
    {
        count = 0U;
    }
    count++;
    be32_write(buf, NVS_OFF_COUNT_0, count);

    /* --- Persist the modified block to Data Flash. --- */
    st = flash_hp_init();
    if (st == FLASH_HP_OK)
    {
        (void)nvs_erase_and_write(buf);
        flash_hp_exit();
    }
    /* Even if the flash write failed, trigger the hardware reset.
     * The crash reason may not survive, but we still stop the runaway. */

    debug_print("[SAFE_RESET] Triggering reset: reason=0x%08lx  count=%lu\r\n",
                (unsigned long)reason, (unsigned long)count);

    hw_system_reset();   /* Never returns */
}

void safe_reset_check_stacks(void)
{
    uint32_t bad_task = OS_StackOverflowCheck();
    if (bad_task != 0U)
    {
        debug_print("[SAFE_RESET] Stack overflow on task index %lu — resetting!\r\n",
                    (unsigned long)(bad_task - 1U));
        safe_reset_trigger(SAFE_RESET_REASON_STACK_OVERFLOW);
        /* safe_reset_trigger() never returns */
    }
}

/* -----------------------------------------------------------------------
 * Fault handler support
 *
 * safe_reset_trigger_raw() is a fault-safe variant that:
 *   - Skips debug_print() (USB CDC may be in a broken state in a fault)
 *   - Exits P/E mode first so Data Flash is readable before nvs_read_block()
 *   - Always calls hw_system_reset(), even if flash write times out
 *
 * Called from the four Cortex-M33 fault handlers below.  All four run in
 * Handler mode on MSP, so the stack frames they build are on the (intact)
 * Main Stack, not on the potentially-corrupted Process Stack.
 * ----------------------------------------------------------------------- */
static void safe_reset_trigger_raw(uint32_t reason)
{
    uint8_t           buf[DATA_FLASH_BLOCK_SIZE];
    uint32_t          count;
    flash_hp_status_t st;

    /* If a prior P/E sequence was interrupted (e.g., another task was in
     * flash_hp_init when the fault fired), exit P/E mode first so the
     * memory-mapped read in nvs_read_block() does not cause a second fault. */
    flash_hp_exit();   /* Harmless if already in read mode. */

    /* Read current NVS block (memory-mapped, read mode is now restored). */
    nvs_read_block(buf);

    /* Write crash fields. */
    buf[NVS_OFF_SAFE_MAGIC_0] = SAFE_RESET_NVS_MAGIC_0;
    buf[NVS_OFF_SAFE_MAGIC_1] = SAFE_RESET_NVS_MAGIC_1;
    buf[NVS_OFF_SAFE_MAGIC_2] = SAFE_RESET_NVS_MAGIC_2;
    buf[NVS_OFF_SAFE_MAGIC_3] = SAFE_RESET_NVS_MAGIC_3;
    be32_write(buf, NVS_OFF_REASON_0, reason);

    count = be32_read(buf, NVS_OFF_COUNT_0);
    if (count == 0xFFFFFFFFUL)
    {
        count = 0U;
    }
    count++;
    be32_write(buf, NVS_OFF_COUNT_0, count);

    /* Persist to Data Flash.  If flash_hp_init() times out the NVS write is
     * skipped but hw_system_reset() still runs — we recover regardless. */
    st = flash_hp_init();
    if (st == FLASH_HP_OK)
    {
        (void)nvs_erase_and_write(buf);
        flash_hp_exit();
    }

    hw_system_reset();   /* Never returns. */
}

/**
 * @brief Shared entry point for all Cortex-M33 fault handlers.
 *
 * Disables interrupts first (prevents a second fault from re-entering),
 * then calls the raw (no-debug_print) reset path.
 */
static void fault_enter(void)
{
    /* Mask all configurable-priority interrupts. */
    __asm volatile("cpsid i" ::: "memory");
    safe_reset_trigger_raw(SAFE_RESET_REASON_FAULT);
    /* hw_system_reset() inside safe_reset_trigger_raw() never returns.
     * This loop is unreachable but silences compiler warnings. */
    for (;;)
    {
        __asm volatile("nop");
    }
}

/* -----------------------------------------------------------------------
 * Cortex-M33 fault handlers
 *
 * These override the weak default symbols from the device startup file.
 * Without them the CPU would spin in an empty loop on any fault, making
 * the MCU appear completely dead (no LED blink, no serial output).
 *
 * After the next boot the crash will be visible in the boot log:
 *   [SAFE_RESET] Previous crash detected: reason=0x00000002  count=N
 * ----------------------------------------------------------------------- */

/** Hard Fault — catches all escalated faults and unhandled exceptions. */
void HardFault_Handler(void)
{
    fault_enter();
}

/** Memory Management Fault — MPU violation or execute-never region. */
void MemManage_Handler(void)
{
    fault_enter();
}

/** Bus Fault — invalid memory access (e.g., Data Flash while in P/E mode). */
void BusFault_Handler(void)
{
    fault_enter();
}

/** Usage Fault — undefined instruction, divide-by-zero, unaligned access. */
void UsageFault_Handler(void)
{
    fault_enter();
}
