# HW_RA6M5_ClockTree

Tags: #done #hardware #clock

Clock generation circuit for R7FA6M5BH3CFC (Cortex-M33). Controls all peripheral clock sources and dividers. Firmware clock driver documented in [[FW_Clock_Driver]]. Write-protection of clock registers covered in [[HW_RA6M5_RWP]].

---

## Clock Sources

| Source | Frequency | Default? | Control Register |
|--------|-----------|----------|-----------------|
| MOCO | 8 MHz | Yes (reset default) | MOCOCR @ SYSC+0x038 |
| HOCO | 16/24/32/48/64 MHz | No | HOCOCR @ SYSC+0x036 |
| LOCO | 32.768 kHz | No | — |
| MOSC | External oscillator | No | MOSCCR |
| PLL | Derived from HOCO/MOSC | No | PLLCCR @ SYSC+0x028 |

SYSC base address: `0x4001E000`

---

## SCKSCR — System Clock Source Control (SYSC+0x026)

Bits [2:0] = CKSEL. Selects the active system clock source.

| CKSEL | Source |
|-------|--------|
| 0x00 | HOCO |
| 0x01 | MOCO (reset default) |
| 0x02 | LOCO |
| 0x03 | MOSC |
| 0x05 | PLL |

Write requires PRCR unlock (PRC0=1). See [[HW_RA6M5_RWP]].

---

## SCKDIVCR — System Clock Division Control (SYSC+0x020)

32-bit register. Applies independent dividers to each clock domain.

| Field | Bits | Clock Domain |
|-------|------|-------------|
| PCKD | [2:0] | PCLKD |
| PCKB | [10:8] | PCLKB (used by RIIC and many peripherals) |
| PCKA | [14:12] | PCLKA (used by SCI on RA6M5) |
| BCK | [18:16] | BCLK |
| ICK | [26:24] | ICLK (CPU) |
| FCK | [30:28] | FCLK (Flash) |

Divider encoding: 0=/1, 1=/2, 2=/4, 3=/8, 4=/16, 5=/32, 6=/64.

Reset default: `0x00000000` (all /1). Write requires PRCR unlock.

---

## Current Firmware Clock Tree

```
XTAL (24 MHz)
  └─ PLL = XTAL / 3 * 25.0 = 200 MHz
      └─ ICLK  = 200 MHz (/1)
      └─ PCLKA = 100 MHz (/2)  ← used by SCI
      └─ PCLKB =  50 MHz (/4)  ← used by RIIC
      └─ PCLKC =  50 MHz (/4)
      └─ PCLKD = 100 MHz (/2)
      └─ BCLK  = 100 MHz (/2)
      └─ FCLK  =  50 MHz (/4)
```

`CLK_Init()` in [[FW_Clock_Driver]] writes this state explicitly during reset.

---

## Oscillation Stabilization

OSCSF register (SYSC+0x03C):
- Bit 0 = HOCOSF: HOCO stable flag. Wait for 1 before switching to HOCO.
- Bit 3 = MOSCSF: main oscillator stable flag.
- Bit 5 = PLLSF: PLL stable flag.

Current firmware waits for both `MOSCSF` and `PLLSF` before switching `SCKSCR` to PLL.

---

## References

RA6M5 Hardware Manual §8 (Clock Generation Circuit), §9 (Module Stop Control).
