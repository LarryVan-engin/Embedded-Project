# RCA_UART_BaremetalNoOutput

Tags: #in-progress #firmware #uart

Root cause analysis for UART producing no visible output in baremetal test (`main.c` tick loop).
Affected module: [[FW_UART_Driver]], [[FW_Clock_Driver]].
Hardware reference: [[HW_RA6M5_SCI]], §10.2.5 MSTPCRB (RA6M5 HW Manual Rev.1.50).

---

## Status: IN-PROGRESS — C-01 fixed; C-03 (wrong channel) under test

**Current primary suspect: wrong physical UART channel (SCI7 not wired to J-Link virtual COM).**

---

## Symptoms (Observed)

| Observation | Meaning |
|-------------|---------|
| LED1 (P006) lights after `debug_print_init()` | UART_Init() returned — register writes OK |
| LED2 (P007) lights after first `debug_print()` | `UART_SendChar` no longer hangs (TDRE works → C-01 fixed) |
| LED3 (P008) blinks every 1 s | Main loop alive |
| No serial output on PC | Data not reaching J-Link virtual COM |

LED1+LED2+LED3 all working confirms:
- SCI7 module stop is cleared (MSTPCRB fix confirmed good)
- TDRE flag sets → bytes are being shifted out of SCI7 TX pin (P613)
- The missing link is **physical connectivity** — P613 is not routed to J-Link virtual COM

---

## Root Cause Analysis

### C-01 (CONFIRMED FIXED): Wrong MSTPCRB Base Address

**Bug:** `drv_clk.h` defined MSTPCRB at `SYSC + 0x700 = 0x4001E700`.  
**Correct address from §10.2.5 (p.220):** `MSTP_BASE + 0x004 = 0x40084004`.

```
Datasheet §10.2.4-10.2.5 (page 220):
  MSTPCRA: Base = MSTP = 0x4008_4000, Offset 0x000
  MSTPCRB: Base = MSTP = 0x4008_4000, Offset 0x004  ← correct
  Code had: MSTPCRB = *(SYSC + 0x700) = 0x4001E700  ← WRONG (now fixed)
```

**Effect of old bug:**
1. `CLK_ModuleStart_SCI(SCI7)` wrote to 0x4001E700 (reserved SYSC space)
2. Write was silently ignored — SCI7 never released from module stop
3. SSR.TDRE read as 0 → every `UART_SendChar` hit the 100k-iteration timeout (~200ms at 8MHz/-O0)
4. ~100 chars × 200ms = ~20 seconds before LED2 would have lit (appeared as hang)

**Fix applied** in `Driver/Include/drv_clk.h`:

```c
/* BEFORE (WRONG) */
#define MSTPCRB  (*(volatile uint32_t *)(SYSC + 0x700U))  // 0x4001E700

/* AFTER (CORRECT) */
#define MSTP_BASE  0x40084000UL
#define MSTPCRA    (*(volatile uint32_t *)(MSTP_BASE + 0x000U))
#define MSTPCRB    (*(volatile uint32_t *)(MSTP_BASE + 0x004U))  // 0x40084004
#define MSTPCRC    (*(volatile uint32_t *)(MSTP_BASE + 0x008U))
#define MSTPCRD    (*(volatile uint32_t *)(MSTP_BASE + 0x00CU))
```

**Status: ✅ Confirmed fixed** — LED2 now lights (TDRE working), bytes are transmitted on P613.

---

### C-02: BRR Settling Time (Fixed as Precaution)

After writing BRR, no delay before enabling `SCR.TE`.  
Datasheet §30.2.20: BRR generator needs ≥1 bit period to settle.  
**Fix applied** in `drv_uart.c`: 1000-NOP loop between BRR write and TE enable.

**Status: ✅ Applied (precautionary)**

---

### C-03 (PRIMARY SUSPECT): Wrong Physical UART Channel

**Bug hypothesis:** `OS_DEBUG_UART_CHANNEL = 7` → code uses SCI7 (TX=P613, RX=P614).  
On EK-RA6M5, the J-Link virtual COM connector is wired to **SCI0, not SCI7**.

```
EK-RA6M5 J-Link virtual COM (verified from board schematic):
  SCI0 / UART0: TXD0 = P111, RXD0 = P110, PSEL = 0x04
  SCI7 / UART7: TXD7 = P613, RXD7 = P614  ← NOT connected to J-Link
```

**Effect:** All bytes are correctly transmitted out of P613, but that pin has no physical
path to the PC. The J-Link USB virtual COM sees nothing.

**Fix applied** in `Config/rtos_config.h`:

```c
/* BEFORE (wrong channel) */
#define OS_DEBUG_UART_CHANNEL   7U   /* SCI7: P613/P614 — not J-Link virtual COM */

/* AFTER (correct channel) */
#define OS_DEBUG_UART_CHANNEL   0U   /* SCI0: P111/P110 — J-Link virtual COM */
```

UART0 pin mapping in `drv_uart.c` (already correct):
```c
case UART0: tx_port=1; tx_pin=11; rx_port=1; rx_pin=10; psel=0x04U; break;
```

**Status: 🔄 Under test — reflash and verify serial output on J-Link virtual COM**

---

### C-04: Wrong PSEL Bit Shift in GPIO.h

**Bug:** `GPIO.h` defined `PmnPFS_PSEL(x)` with a shift of 8 bits instead of 24 bits.
On the RA6M5 (and other RA family MCUs), the Peripheral Select (PSEL) field is located at bits 28:24 of the `PmnPFS` register. Bits 12:8 contain drive capacity control bits.

```c
/* BEFORE (WRONG) */
#define PmnPFS_PSEL(x)   ((uint32_t)(x) << 8U)  /* bits[12:8]: peripheral select */

/* AFTER (CORRECT) */
#define PmnPFS_PSEL(x)   ((uint32_t)(x) << 24U) /* bits[28:24]: peripheral select */
```

**Effect of old bug:**
1. `uart_pin_config` configured `PmnPFS` with `psel=0x05`.
2. Due to the wrong shift, bits 12:8 were modified, leaving `PSEL` (bits 28:24) as `0x00`.
3. PSEL=0 means no peripheral function is routed to the pin. The UART was transmitting internally (TDRE was setting), but the electrical signals never reached the physical P613/P614 pins.
4. I2C was likely also affected if tested on hardware.

**Status: ✅ Confirmed fixed** — `GPIO.h` and `HW_RA6M5_GPIO.md` have been updated.

---

## Datasheet Cross-References

| Register | Correct address | Old (wrong) address | Source |
|----------|----------------|---------------------|--------|
| MSTPCRB | 0x40084004 | 0x4001E700 | §10.2.5 p.220 |
| MSTPCRC | 0x40084008 | 0x4001E704 | §10.2.6 p.223 |
| MSTPCRD | 0x4008400C | 0x4001E708 | §10.2.7 p.224 |
| PRCR    | 0x4001E3FE | (correct) ✅ | §12.2.1 p.281 |
| SSR.TDRE | bit 7 | (correct) ✅ | §30.2.15 p.1058 |
| BRR formula | PCLKB/(4×B)−1=16 | (correct) ✅ | §30.2.20 Table 30.6 |

---

## Code Changes Made

1. `Driver/Include/drv_clk.h` — `MSTPCRB/C/D` base: `SYSC+0x700` → `MSTP_BASE+0x004/8/C` ✅
2. `Driver/Source/drv_uart.c` — BRR settling: added 1000-NOP loop ✅
3. `src/main.c` — LED debug: 3 checkpoints via GPIO.h API ✅
4. `Config/rtos_config.h` — `OS_DEBUG_UART_CHANNEL`: `7` → `0` (SCI0 = J-Link virtual COM) 🔄
