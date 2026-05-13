/*
 * drv_flash_hp.c — Flash HP Peripheral Driver for RA6M5 (Data Flash).
 *
 * MISRA-C:2012 compliance:
 *   - Rule 11.3 / 11.4: volatile pointers only via named macros (no casts in logic)
 *   - Rule 14.4: no complex pointer arithmetic; addresses computed as uint32_t
 *   - Rule 15.5: single return point where practical; early return on param error
 *   - Rule 17.7: all return values of called functions checked
 *   - D4.1 / Rule 1.3: array bounds checked before access
 *
 * Hardware manual: RA6M5 Group User Manual §44 (Flash HP).
 *
 * Data Flash P/E mode entry sequence (§44.4.3):
 *   1. Set FENTRYR = 0xAA80 (data flash P/E mode enable)
 *   2. Set FPMCR  = 0x10 (FMS1=1)
 *   3. Set FPMCR  = 0x12 (FMS1=1, FMS0=1)
 *   (Remain in P/E; exit by reversing the sequence)
 */

#include "drv_flash_hp.h"
#include <stdint.h>
#include <stddef.h>  /* NULL */

/* -----------------------------------------------------------------------
 * Internal timeout limit — Data Flash operations take <10 ms at 200 MHz.
 * 5 000 000 iterations at ~1 ns/iter = ~5 ms headroom.
 * ----------------------------------------------------------------------- */
#define FLASH_WAIT_TIMEOUT  5000000UL

#define FLASH_HP_DF_PROGRAM_BYTES      4UL
#define FLASH_HP_DF_PROGRAM_HALFWORKS  2U

static void flash_hp_issue_cmd8(uint8_t cmd)
{
    FLASH_HP_CMD_REG8 = cmd;
}

static void flash_hp_issue_cmd16(uint16_t value)
{
    FLASH_HP_CMD_REG16 = value;
}

static flash_hp_status_t flash_hp_status_clear(void)
{
    uint32_t timeout = FLASH_WAIT_TIMEOUT;

    flash_hp_issue_cmd8(FLASH_CMD_STATUS_CLR);

    while ((FSTATR & FSTATR_FRDY) == 0UL)
    {
        if (timeout == 0UL)
        {
            return FLASH_HP_ERR_TIMEOUT;
        }

        timeout--;
    }

    if ((FASTAT & (FASTAT_DFAE | FASTAT_CFAE)) != 0U)
    {
        FASTAT = 0U;
    }

    return FLASH_HP_OK;
}

/* -----------------------------------------------------------------------
 * Internal helper: wait for FSTATR.FRDY == 1 (flash sequencer idle).
 * Returns FLASH_HP_OK on ready, FLASH_HP_ERR_TIMEOUT on expiry.
 * ----------------------------------------------------------------------- */
static flash_hp_status_t flash_hp_wait_ready(void)
{
    uint32_t timeout = FLASH_WAIT_TIMEOUT;

    while ((FSTATR & FSTATR_FRDY) == 0UL)
    {
        timeout--;
        if (timeout == 0UL)
        {
            return FLASH_HP_ERR_TIMEOUT;
        }
    }

    /* Check hardware error flags after ready */
    if (((FSTATR & FSTATR_ERROR_MASK) != 0UL) || ((FASTAT & FASTAT_CMDLK) != 0U))
    {
        (void)flash_hp_status_clear();
        return FLASH_HP_ERR_HW;
    }

    return FLASH_HP_OK;
}

/* -----------------------------------------------------------------------
 * Internal helper: wait until FENTRYR reaches the expected mode value.
 * For FLASH HP on RA6M5, Data Flash P/E entry/exit is confirmed via FENTRYR,
 * not by polling FRDY immediately after writing the entry key.
 * ----------------------------------------------------------------------- */
static flash_hp_status_t flash_hp_wait_fentryr(uint16_t expected)
{
    uint32_t timeout = FLASH_WAIT_TIMEOUT;

    while (FENTRYR != expected)
    {
        timeout--;
        if (timeout == 0UL)
        {
            return FLASH_HP_ERR_TIMEOUT;
        }
    }

    return FLASH_HP_OK;
}

/* -----------------------------------------------------------------------
 * Internal helper: clear any pending flash errors.
 * ----------------------------------------------------------------------- */
static void flash_hp_clear_errors(void)
{
    if (((FSTATR & FSTATR_ERROR_MASK) != 0UL) || ((FASTAT & (FASTAT_DFAE | FASTAT_CFAE | FASTAT_CMDLK)) != 0U))
    {
        (void)flash_hp_status_clear();

        /* Brief wait for status clear to propagate */
        for (volatile uint32_t i = 0U; i < 100U; i++)
        {
            __asm volatile("nop");
        }
    }
}

/* -----------------------------------------------------------------------
 * Internal helper: validate a Data Flash byte range without overflow.
 * ----------------------------------------------------------------------- */
static uint8_t flash_hp_range_valid(uint32_t addr, uint32_t len)
{
    uint32_t offset;

    if ((addr < DATA_FLASH_BASE) || (len == 0UL))
    {
        return 0U;
    }

    offset = addr - DATA_FLASH_BASE;
    if (offset >= DATA_FLASH_SIZE)
    {
        return 0U;
    }

    if (len > (DATA_FLASH_SIZE - offset))
    {
        return 0U;
    }

    return 1U;
}

/* -----------------------------------------------------------------------
 * flash_hp_init — Enter Data Flash P/E mode.
 *
 * Sequence per RA6M5 §44.4.3:
 *   1. FENTRYR = 0xAA80 (enable data flash P/E entry)
 *   2. Wait FSTATR.FRDY = 1
 *   3. FPMCR   = 0x10   (FMS1=1, start transition)
 *   4. FPMCR   = 0x12   (FMS1=1, FMS0=1, data flash PE mode)
 * ----------------------------------------------------------------------- */
flash_hp_status_t flash_hp_init(void)
{
    flash_hp_status_t status;

    /* Allow FLASH HP program/erase access before entering Data Flash P/E mode. */
    FWEPROR = FWEPROR_PE_ENABLE;

    status = flash_hp_wait_ready();
    if (status != FLASH_HP_OK)
    {
        return status;
    }

    /* Notify the FLASH HP sequencer of the current 50 MHz FCLK. */
    FPCKAR = (uint16_t)(FPCKAR_KEY_FCLK | FPCKAR_FCLK_MHZ);

    /* Step 1: enable data flash P/E mode in FENTRYR */
    FENTRYR = FENTRYR_KEY_DATA_PE;

    /* FLASH HP confirms Data Flash P/E transition by reflecting 0x0080 in FENTRYR. */
    status = flash_hp_wait_fentryr((uint16_t)(FENTRYR_KEY_DATA_PE & 0x00FFU));
    if (status != FLASH_HP_OK)
    {
        return status;
    }

    flash_hp_clear_errors();

    return FLASH_HP_OK;
}

/* -----------------------------------------------------------------------
 * flash_hp_exit — Exit P/E mode and return to read mode.
 * Reverse the FPMCR/FENTRYR sequence.
 * ----------------------------------------------------------------------- */
void flash_hp_exit(void)
{
    /* FLASH HP returns to read mode by clearing the P/E mode bits in FENTRYR. */
    FENTRYR = FENTRYR_KEY_READ;

    (void)flash_hp_wait_fentryr(0U);
}

/* -----------------------------------------------------------------------
 * flash_hp_erase — Erase one or more 64-byte Data Flash blocks.
 *
 * Block Erase command sequence (§44.4.4):
 *   1. Issue FLASH_CMD_ERASE to the target block address
 *   2. Issue FLASH_CMD_ERASE_CFM (0xD0) to the same address
 *   3. Wait FRDY
 * Each block must be erased individually.
 * ----------------------------------------------------------------------- */
flash_hp_status_t flash_hp_erase(uint32_t addr, uint32_t num_blocks)
{
    flash_hp_status_t status;
    uint32_t          block_addr;
    uint32_t          block_idx;
    uint32_t          block_offset;

    /* Parameter validation (MISRA Rule 14.5, D4.1) */
    if (num_blocks == 0UL)
    {
        return FLASH_HP_ERR_PARAM;
    }
    if ((addr & (DATA_FLASH_BLOCK_SIZE - 1UL)) != 0UL)   /* must be block-aligned */
    {
        return FLASH_HP_ERR_PARAM;
    }
    if (flash_hp_range_valid(addr, DATA_FLASH_BLOCK_SIZE) == 0U)
    {
        return FLASH_HP_ERR_PARAM;
    }

    block_offset = addr - DATA_FLASH_BASE;
    if (num_blocks > ((DATA_FLASH_SIZE - block_offset) / DATA_FLASH_BLOCK_SIZE))
    {
        return FLASH_HP_ERR_PARAM;
    }

    block_addr = addr;

    /* Match FSP data-flash erase path: use erasure priority mode. */
    FCPSR = 1U;

    for (block_idx = 0UL; block_idx < num_blocks; block_idx++)
    {
        /* Set the block start address in FSADDR */
        FSADDR = block_addr;

        /* Issue Block Erase command to the data flash address space.
         * On the Flash HP, commands are issued by writing to the flash address.
         * Use the address as a write-1 trigger via the Flash Control Interface.
         * Per §44.4.4: write erase command code to FACI command-issuing area.
         *
         * The command-issuing area for Data Flash is at 0xFE7F5000 + word offset. */
        flash_hp_issue_cmd8(FLASH_CMD_ERASE);
        flash_hp_issue_cmd8(FLASH_CMD_ERASE_CFM);

        /* Wait for erase completion */
        status = flash_hp_wait_ready();
        if (status != FLASH_HP_OK)
        {
            return status;
        }

        block_addr += DATA_FLASH_BLOCK_SIZE;
    }

    return FLASH_HP_OK;
}

/* -----------------------------------------------------------------------
 * flash_hp_write — Program Data Flash in 4-byte word units.
 *
 * Program command sequence (§44.4.5):
 *   1. Issue FLASH_CMD_PROGRAM (0xE8) to FACI area
 *   2. Write word count (bytes/4) as byte to FACI
 *   3. Write data words (4 bytes each) to FACI
 *   4. Issue FLASH_CMD_PROGRAM_CFM (0xD0) to FACI
 *   5. Wait FRDY
 *
 * Maximum words per command: 64 (= 256 bytes).  If len > 256, the
 * function loops, issuing multiple program commands.
 * ----------------------------------------------------------------------- */
flash_hp_status_t flash_hp_write(uint32_t addr, uint8_t const * const p_src, uint32_t len)
{
    flash_hp_status_t status;
    uint32_t          bytes_remaining;
    uint32_t          write_addr;
    uint32_t          src_offset;

    /* Parameter validation */
    if (p_src == NULL)
    {
        return FLASH_HP_ERR_PARAM;
    }
    if (len == 0UL)
    {
        return FLASH_HP_ERR_PARAM;
    }
    if ((len & (DATA_FLASH_WRITE_WORD_SIZE - 1UL)) != 0UL)  /* must be multiple of 4 */
    {
        return FLASH_HP_ERR_PARAM;
    }
    if ((addr & (DATA_FLASH_WRITE_WORD_SIZE - 1UL)) != 0UL)  /* must be word-aligned */
    {
        return FLASH_HP_ERR_PARAM;
    }
    if (flash_hp_range_valid(addr, len) == 0U)
    {
        return FLASH_HP_ERR_PARAM;
    }

    bytes_remaining = len;
    write_addr      = addr;
    src_offset      = 0UL;

    while (bytes_remaining > 0UL)
    {
        /* Set destination address in FSADDR */
        FSADDR = write_addr;

        /* FLASH HP data flash programs 4 bytes per command, with a count of two halfwords. */
        flash_hp_issue_cmd8(FLASH_CMD_PROGRAM);
        flash_hp_issue_cmd8(FLASH_HP_DF_PROGRAM_HALFWORKS);
        flash_hp_issue_cmd16((uint16_t)((uint16_t)p_src[src_offset + 0UL]
                           | ((uint16_t)p_src[src_offset + 1UL] << 8U)));
        flash_hp_issue_cmd16((uint16_t)((uint16_t)p_src[src_offset + 2UL]
                           | ((uint16_t)p_src[src_offset + 3UL] << 8U)));
        flash_hp_issue_cmd8(FLASH_CMD_PROGRAM_CFM);

        /* Wait for program completion */
        status = flash_hp_wait_ready();
        if (status != FLASH_HP_OK)
        {
            return status;
        }
        src_offset      += FLASH_HP_DF_PROGRAM_BYTES;
        write_addr      += FLASH_HP_DF_PROGRAM_BYTES;
        bytes_remaining -= FLASH_HP_DF_PROGRAM_BYTES;
    }

    return FLASH_HP_OK;
}

/* -----------------------------------------------------------------------
 * flash_hp_verify — Read-back verify after flash_hp_exit().
 *
 * Data Flash appears as a normal read-mapped region at 0x08000000 in
 * read mode. Compare byte by byte.
 * ----------------------------------------------------------------------- */
flash_hp_status_t flash_hp_verify(uint32_t addr, uint8_t const * const p_ref, uint32_t len)
{
    uint32_t idx;

    /* Parameter validation */
    if (p_ref == NULL)
    {
        return FLASH_HP_ERR_PARAM;
    }
    if (len == 0UL)
    {
        return FLASH_HP_ERR_PARAM;
    }
    if (flash_hp_range_valid(addr, len) == 0U)
    {
        return FLASH_HP_ERR_PARAM;
    }

    for (idx = 0UL; idx < len; idx++)
    {
        uint8_t flash_byte;
        /* Access Data Flash at mapped address — cast via uintptr_t per MISRA 11.6 */
        flash_byte = *(volatile uint8_t *)(uintptr_t)(addr + idx);

        if (flash_byte != p_ref[idx])
        {
            return FLASH_HP_ERR_VERIFY;
        }
    }

    return FLASH_HP_OK;
}
