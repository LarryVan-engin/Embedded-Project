# HW_RA6M5_RWP

Tags: #done #hardware #system

Register Write Protection (PRCR) on RA6M5. Protects clock generation and module stop control registers from accidental writes. Must be unlocked before writing SCKSCR, SCKDIVCR, or MSTPCR registers. Firmware implementation: [[FW_RWP_Driver]].

---

## PRCR — Protection Register (SYSC+0x3FE)

16-bit register. Write requires the 0xA5 key in the upper byte.

```
PRCR write value = (0xA5 << 8) | enable_bits
```

| Bit | Name | Protects |
|-----|------|----------|
| 1 | PRC1 | MSTPCR module stop control registers (MSTPCRB/C/D) |
| 0 | PRC0 | Clock generation circuit registers (SCKSCR, SCKDIVCR, HOCOCR, ...) |

Reading PRCR: upper byte always reads back as 0xA5. Lower byte reflects current lock state (0=locked, 1=unlocked for each bit).

---

## Unlock/Lock Pattern

```c
/* Unlock: key + both PRC0 and PRC1 enabled */
PRCR = (uint16_t)((0xA5U << 8U) | 0x03U);

/* ... write protected registers ... */

/* Lock: key + lower byte = 0x00 */
PRCR = (uint16_t)(0xA5U << 8U);
```

Unlocking both bits simultaneously (0x03) is safe and simplifies the driver — separate unlock functions for clock vs. MSTP are not necessary.

---

## Scope

PRCR applies only to the specific registers listed above. GPIO, SCI, and RIIC configuration registers are NOT protected by PRCR — they have their own protection (PWPR for PFS, IICRST for RIIC).

---

## References

RA6M5 Hardware Manual §13 (Register Write Protection).
