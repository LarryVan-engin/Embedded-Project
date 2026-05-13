#ifndef DRV_RWP_H
#define DRV_RWP_H
#include <stdint.h>
#include "drv_common.h"

/*
 * drv_rwp.h — Register Write Protection (PRCR) for RA6M5.
 *
 * Replaces: RWP.h
 *
 * PRCR (Protection Register) is at SYSC + 0x3FE.
 * Write key 0xA5 to upper byte + enable bits in lower byte:
 *   PRC0 (bit 0): clock generation circuit registers
 *   PRC1 (bit 1): module stop control registers (MSTPCR)
 */

#define PRCR    (*(volatile uint16_t *)(uintptr_t)(SYSC + 0x3FEU))

/*
 * RWP_Unlock_Clock_MSTP — unlock PRCR for clock and MSTPCR writes.
 * Must be called before writing SCKSCR, SCKDIVCR, or MSTPCR registers.
 */
void RWP_Unlock_Clock_MSTP(void);

/*
 * RWP_Lock_Clock_MSTP — re-lock PRCR after writes are complete.
 */
void RWP_Lock_Clock_MSTP(void);

#endif /* DRV_RWP_H */
