# RCA_UART_BRR_SEMR

Tags: #done #firmware #uart

Root cause analysis for incorrect UART baud rate — wrong PCLKB value and swapped SEMR bit positions. Affected module: [[FW_UART_Driver]]. Hardware reference: [[HW_RA6M5_SCI]].

---

## Symptoms

- UART output at incorrect baud rate — communication garbage or no response
- Baud rate was 16× too slow at intended 115200 baud

---

## Hardware Root Cause

RA6M5 SCI SEMR register (§34 Table 34.12) has two independent baud-rate modifier bits:
- **Bit 6 = BGDM** (Baud Rate Generator Double-Speed Mode)
- **Bit 4 = ABCS** (Asynchronous Base Clock Select: 1 = 8 clocks/bit)

With both BGDM=1 and ABCS=1, the effective clock divider per bit is **/4**, giving the formula:
```
BRR = PCLKB / (4 × baudrate) - 1
```

Without these bits (default state), the divider is /64 — producing a baud rate 16× lower than intended.

---

## Root Cause in Code

Two compounded errors:

**1. PCLKB set to 2 MHz instead of 8 MHz:**

```c
#define PCLKB  2000000UL   /* WRONG: MOCO is 8 MHz at reset */
```

RA6M5 resets with MOCO at 8 MHz and SCKDIVCR all /1. The constant was never updated from a placeholder value.

**2. SEMR bit positions swapped:**

```c
#define SEMR_BGDM (1U << 5)   /* WRONG: BGDM is bit 6 */
#define SEMR_ABCS (1U << 6)   /* WRONG: ABCS is bit 4 */
```

Bit 5 is NFEN (Noise Filter Enable), not BGDM. Bit 6 is BGDM. Setting `SEMR_ABCS` at bit 6 accidentally set BGDM (doubling speed), while ABCS was never set, and NFEN was set unintentionally.

---

## Fix

```c
#define PCLKB     8000000UL    /* MOCO 8 MHz — confirmed by CLK_Init in [[FW_Clock_Driver]] */
#define SEMR_BGDM (1U << 6)   /* Bit 6: Baud Rate Generator Double-Speed Mode */
#define SEMR_ABCS (1U << 4)   /* Bit 4: Asynchronous Base Clock Select         */
```

BRR at 115200 baud = 8 000 000 / (4 × 115 200) - 1 = **16**.
Actual baud = 117 647 Hz (2.1% error — within the 5% maximum specified by UART receivers).
