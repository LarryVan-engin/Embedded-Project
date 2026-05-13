# Driver Code Review & Implementation Plan
**Project:** TESTING_2 — Renesas RA6M5 Bare-metal Driver  
**Target:** R7FA6M5BH3CFC (Cortex-M33, EK-RA6M5)  
**Reviewer:** Senior Embedded  
**Date:** 2026-04-22  

---

## Table of Contents
1. [Bug Report](#bug-report)
   - [Critical Bugs](#critical-bugs)
   - [High Severity Bugs](#high-severity-bugs)
   - [Medium Severity Bugs](#medium-severity-bugs)
   - [Low Severity Issues](#low-severity-issues)
2. [Structural Problems](#structural-problems)
3. [Driver File Reorganization Plan](#driver-file-reorganization-plan)
4. [Implementation Plan](#implementation-plan)

---

## Bug Report

### Critical Bugs

---

#### BUG-01 — `SYSC` macro defined twice (redefinition)
| Field | Detail |
|---|---|
| **Files** | `Driver/Include/LPM.h:5`, `Driver/Include/RWP.h:5` |
| **Severity** | CRITICAL |

**Problem:**  
Both `LPM.h` and `RWP.h` define the same macro independently:
```c
// LPM.h line 5
#define SYSC  0x4001E000UL

// RWP.h line 5 — DUPLICATE
#define SYSC  0x4001E000UL
```
`SCI.c` includes both `SCI.h` (which pulls in `LPM.h`) **and** `RWP.h` directly → macro redefinition.
GCC emits a warning; strict compilers treat this as an error.

**Fix:**  
Define `SYSC` once in a shared `drv_common.h`. Remove from both `LPM.h` and `RWP.h`, replace with `#include "drv_common.h"`.

---

#### BUG-02 — `CGC.h` uses `SYSC` without defining or including it
| Field | Detail |
|---|---|
| **File** | `Driver/Include/CGC.h:6` |
| **Severity** | CRITICAL |

**Problem:**  
```c
// CGC.h line 6 — SYSC is used but never defined here
#define HOCOCR  *(volatile uint8_t*)(uintptr_t)(SYSC + 0x036)
```
The comment says *"SYSC base is defined in LPM.h"* but there is no `#include "LPM.h"` in the file.
Any translation unit that includes `CGC.h` without first including `LPM.h` will fail to compile.

**Fix:**  
Add `#include "drv_common.h"` (or `"LPM.h"` as interim) at the top of `CGC.h`.

---

#### BUG-03 — `PCLKB` value incorrect; BRR formula does not match SEMR configuration
| Field | Detail |
|---|---|
| **Files** | `Driver/Include/SCI.h:118`, `Driver/Source/SCI.c:193-194` |
| **Severity** | CRITICAL |

**Problem:**  
```c
// SCI.h:118
#define PCLKB 2000000UL   // WRONG — RA6M5 boots from MOCO 8 MHz; no CGC init is performed

// SCI.c:193-194
*SEMR = (uint8_t)(SEMR_ABCS | SEMR_BGDM);            // ABCS=1, BGDM=1 → effective /4 divider
*BRR  = (uint8_t)((PCLKB / (16UL * baudrate)) - 1);  // formula uses /16 — MISMATCH
```
With `ABCS=1` and `BGDM=1` the correct baud rate formula is:
```
BRR = PCLKB / (4 × baudrate) − 1
```
The code uses the plain `/16` formula. At 115200 baud the integer truncation accidentally yields
the same physical output (~125 kbaud) whether PCLKB is treated as 2 MHz or 8 MHz, hiding the bug.
**At any other baud rate (e.g. 9600, 57600, 230400) the calculated BRR will be wrong.**

**Fix:**
```c
#define PCLKB  8000000UL   // MOCO default; update when CGC is configured

// BRR formula must match SEMR setting:
// SEMR: ABCS=1, BGDM=1  →  effective clock = PCLKB / 4
*BRR = (uint8_t)((PCLKB / (4UL * baudrate)) - 1U);
```

---

#### BUG-04 — `UART_SendChar`: manual SSR clear is incorrect and potentially harmful
| Field | Detail |
|---|---|
| **File** | `Driver/Source/SCI.c:245-247` |
| **Severity** | CRITICAL |

**Problem:**  
```c
while (!(*SSR & SSR_TDRE));          // wait TX buffer empty
*TDR = (uint8_t)data;                // hardware auto-clears TDRE on TDR write
*SSR &= (uint8_t)(~(uint8_t)SSR_TDRE);  // ← WRONG: TDRE already cleared by hardware
                                          // writing 0 to SSR can clobber ORER/FER/PER flags
```
On RA6M5 SCI, TDRE is automatically cleared by the hardware when data is written to TDR.
The manual write-back to SSR is redundant **and** risks accidentally clearing receive error flags
(ORER, FER, PER) stored in the same register.

**Fix:** Remove the `*SSR &= ...` line entirely.

---

#### BUG-05 — Uninitialized pointer dereference in UART polling functions
| Field | Detail |
|---|---|
| **File** | `Driver/Source/SCI.c:199-248` (SendChar), `SCI.c:253-301` (ReceiveChar) |
| **Severity** | CRITICAL |

**Problem:**  
```c
void UART_SendChar(UART_t UART_, char data) {
    volatile uint8_t *TDR;   // uninitialized
    volatile uint8_t *SSR;   // uninitialized
    switch (UART_) {
        case UART0: TDR = &SCI0_TDR; SSR = &SCI0_SSR; break;
        // ...
        default: break;      // NO return — TDR/SSR remain garbage pointers
    }
    while (!(*SSR & SSR_TDRE));  // undefined behaviour if default was hit
    *TDR = (uint8_t)data;
```
Same pattern exists in `UART_ReceiveChar`. If an invalid `UART_` value is passed, the function
dereferences uninitialized pointers → **undefined behaviour / hard fault**.

**Fix:** Change `default: break;` to `default: return;` in all four switch blocks, or initialize
pointers to `NULL` and add a null-check before the polling loops.

---

### High Severity Bugs

---

#### BUG-06 — `I2C_Init`: initialisation sequence violates hardware spec
| Field | Detail |
|---|---|
| **File** | `Driver/Source/IIC.c:87-91` |
| **Severity** | HIGH |

**Problem:**  
```c
ICCR1(n) = 0x00;
ICCR1(n) |= (1U << 7);   // ICE=1  ← set BEFORE IICRST
ICCR1(n) |= (1U << 6);   // IICRST=1 ← reset AFTER enabling
ICCR1(n) &= ~(1U << 6);  // release reset
```
RA6M5 Hardware Manual §38.3 requires:
1. Set `IICRST=1` while `ICE=0`
2. Set `ICE=1` (while still in reset)
3. Configure `ICBRL`, `ICBRH`, `ICMR*`
4. Clear `IICRST=0`

Enabling the peripheral before asserting reset can lock up the I2C bus if it was previously active.

**Fix:**
```c
ICCR1(n) = 0x00;                   // ICE=0, IICRST=0
ICCR1(n) |= (1U << 6);             // IICRST=1 (reset first)
ICCR1(n) |= (1U << 7);             // ICE=1 (enable while in reset)
ICBRL(n)  = br;
ICBRH(n)  = br;
// ... other config ...
ICCR1(n) &= (uint8_t)~(1U << 6);  // IICRST=0 (release reset)
```

---

#### BUG-07 — `I2C_Start`: waits for TEND flag after START condition — will hang
| Field | Detail |
|---|---|
| **File** | `Driver/Source/IIC.c:111-114` |
| **Severity** | HIGH |

**Problem:**  
```c
while (ICCR2(n) & (1 << 7));         // wait BBSY=0 (bus free)  — OK
ICCR2(n) |= (1 << 1);                // ST=1: generate START    — OK
while (!(ICCR2(n) & (1 << 7)));      // wait BBSY=1 (bus active) — OK
while (!(ICSR2(n) & (1 << 6)));      // wait TEND=1 after START  — WRONG
```
TEND (Transmit End) is only set after a **data or address byte** has been fully transmitted.
After generating a START condition there is no byte transmission, so TEND never sets.
This loop **hangs indefinitely**.

**Fix:** Remove the `while (!(ICSR2(n) & TEND))` line from `I2C_Start`. TEND should be checked
inside `I2C_Transmit_Address` after the address byte is sent (which it already is at line 162).

---

#### BUG-08 — `I2C_Master_Receive_Data`: ACK/NACK logic is broken
| Field | Detail |
|---|---|
| **File** | `Driver/Source/IIC.c:236-248` |
| **Severity** | HIGH |

**Problem:**  
```c
ICMR3(n) |= (1U << 4);   // ACKBT=1 (schedule NACK) — always?
if (i == (data_length - 2)) {
    ICMR3(n) |= (1U << 3);   // ACKWP=1 — wrong position (must be set BEFORE writing ACKBT)
}
ICMR3(n) &= ~(1U << 4);  // ACKBT=0 (schedule ACK) — immediately reverses the NACK
```
Three problems:
1. ACKBT is set to 1 then immediately cleared to 0 on every byte — no NACK is ever actually sent.
2. ACKWP (write-protect for ACKBT) must be set **before** writing ACKBT, not after.
3. The last byte must send NACK (`ACKBT=1`); all preceding bytes must send ACK (`ACKBT=0`).
   The correct trigger is `i == (data_length - 1)`, not `data_length - 2`.

**Fix:**
```c
for (i = 0; i < data_length; i++) {
    while (!(ICSR2(n) & (1U << 5)));  // wait RDRF=1

    // For the last byte: send NACK. For all others: send ACK.
    ICMR3(n) |= (1U << 3);   // ACKWP=1 (unlock ACKBT write)
    if (i == (data_length - 1)) {
        ICMR3(n) |= (1U << 4);   // ACKBT=1 → NACK
    } else {
        ICMR3(n) &= ~(1U << 4);  // ACKBT=0 → ACK
    }
    ICMR3(n) &= ~(1U << 3);  // ACKWP=0 (lock)

    data[i] = ICDRR(n);
}
```

---

#### BUG-09 — `SEMR_ABCS` and `SEMR_BGDM` bit positions require verification
| Field | Detail |
|---|---|
| **File** | `Driver/Include/SCI.h:120-121` |
| **Severity** | HIGH |

**Problem:**  
```c
#define SEMR_ABCS (1 << 6)   // defined as bit 6
#define SEMR_BGDM (1 << 5)   // defined as bit 5
```
Per RA6M5 User Manual Table 34.12 (SCI SEMR register):
- **BGDM** = bit **6**
- **ABCS** = bit **4**

The current definitions appear swapped/incorrect. If wrong, baud rate generation is misconfigured.

**Fix:** Verify against hardware manual and correct:
```c
#define SEMR_BGDM (1U << 6)   // Baud Rate Generator Double-Speed
#define SEMR_ABCS (1U << 4)   // Asynchronous Base Clock Select (8-clock)
```

---

### Medium Severity Bugs

---

#### BUG-10 — `GPIO_PortToIndex` returns 0 (valid port) for invalid input
| Field | Detail |
|---|---|
| **File** | `Driver/Source/GPIO.c:10` |
| **Severity** | MEDIUM |

**Problem:**  
```c
else return 0U;   // 0 == PORT0 — caller cannot distinguish error from a valid port
```

**Fix:**
```c
else return 0xFFU;  // sentinel: no valid port has index 0xFF
```
All callers that use the return value as an array/register index should check for `0xFF`.

---

#### BUG-11 — `GPIO_MODE_t` speed values are meaningless on RA6M5
| Field | Detail |
|---|---|
| **File** | `Driver/Include/GPIO.h:57-62` |
| **Severity** | MEDIUM |

**Problem:**  
```c
typedef enum {
    GPIO_MODE_IN      = 0b00,
    GPIO_MODE_OUT_10M = 0b01,   // RA6M5 has no drive-speed register
    GPIO_MODE_OUT_2M  = 0b10,   // these are STM32 GPIO_Speed values, copy-pasted
    GPIO_MODE_OUT_50M = 0b11
} GPIO_MODE_t;
```
In `GPIO_Config()`, all three OUT modes execute the identical code: `PORT_PDR(p) |= (1U << pin)`.
The speed suffix is misleading to any user of this driver.

**Fix:** Simplify to:
```c
typedef enum {
    GPIO_MODE_INPUT  = 0,
    GPIO_MODE_OUTPUT = 1
} GPIO_MODE_t;
```

---

#### BUG-12 — Module stop (MSTPCRB) for I2C bypasses the `LPM_Unlock` abstraction
| Field | Detail |
|---|---|
| **Files** | `Driver/Source/IIC.c:13-26`, `Driver/Source/LPM.c` |
| **Severity** | MEDIUM |

**Problem:**  
`LPM_Unlock()` only handles SCI peripherals. `I2C_Clock_Init()` accesses `MSTPCRB` directly,
bypassing the wrapper entirely. This means:
- The abstraction is incomplete and inconsistent.
- Future refactors of module stop logic must touch two different places.

**Fix:** Extend `LPM_Unlock` (or the replacement `drv_clk` module) to cover I2C channels,
and call it uniformly from `I2C_Clock_Init`.

---

#### BUG-13 — `delay_ms` duplicated in `hal_entry.c` and `main.c`
| Field | Detail |
|---|---|
| **Files** | `src/hal_entry.c:37-44`, `src/main.c:18-25` |
| **Severity** | MEDIUM |

**Problem:**  
Identical `static void delay_ms(uint32_t ms)` is copy-pasted into two files.
The calibration comment is also identical, making it a pure duplication.

**Fix:** Move to `src/utils.h` as `static inline` or create `src/utils.c`.

---

### Low Severity Issues

---

#### BUG-14 — `I2C_PINCFG_t` and `I2C_ACK_t` enums declared but never used
| Field | Detail |
|---|---|
| **File** | `Driver/Include/IIC.h:36-56` |
| **Severity** | LOW |

```c
typedef enum { I2C_PIN_A, I2C_PIN_B }       I2C_PINCFG_t;  // unused
typedef enum { I2C_ACK_CONTINUE, I2C_ACK_STOP } I2C_ACK_t;  // unused
```
Dead code. Remove or implement.

---

#### BUG-15 — `main.c` uses relative include paths inconsistently
| Field | Detail |
|---|---|
| **File** | `src/main.c:12-13` |
| **Severity** | LOW |

```c
#include "../Driver/Include/SCI.h"    // relative
#include "../Driver/Include/GPIO.h"   // relative
```
All other source files use `#include "SCI.h"` relying on CMake include directories.
Mixing styles breaks if include paths change.

**Fix:** Use `#include "SCI.h"` and `#include "GPIO.h"` consistently.

---

#### BUG-16 — `test_runner.c`: `g_test_fail_count` counts assertions, not test cases
| Field | Detail |
|---|---|
| **File** | `src/test/test_runner.c:29` |
| **Severity** | LOW |

```c
void test_record_failure(...) {
    g_test_fail_count++;   // incremented per assertion, not per test
}
```
If one test has 3 failing assertions, `g_test_fail_count = 3` but only 1 test failed.
The `== 0` check in `hal_entry.c` is still correct, but the counter semantics are misleading.

**Fix:** Increment `g_test_fail_count` only once per test (already tracked by `s_current_failed`).

---

## Structural Problems

| # | Problem | Affected Files |
|---|---------|---------------|
| S-01 | `LPM.h` misnamed — contains Module Stop Control (MSTPCR), not Low Power Mode logic | `LPM.h`, `LPM.c` |
| S-02 | `SCI.h` has 70+ flat macros (10 channels × 7 registers). Does not scale. | `SCI.h` |
| S-03 | `CGC.c` is an empty stub. Clock is never configured; PCLKB is assumed, not enforced. | `CGC.c` |
| S-04 | `SYSC` base address defined in two files simultaneously | `LPM.h`, `RWP.h` |
| S-05 | No timeout in any busy-wait loop (UART TX/RX, I2C all flags). System can lock up permanently on hardware failure. | `SCI.c`, `IIC.c` |
| S-06 | I2C has no bus recovery mechanism for SCL/SDA stuck-low condition | `IIC.c` |

---

## Driver File Reorganization Plan

### Current Structure (problematic)
```
Driver/
├── Include/
│   ├── CGC.h       ← uses SYSC but doesn't include it
│   ├── GPIO.h      ← OK but MODE enum is wrong
│   ├── IIC.h       ← dead enums, misnamed LSB enum
│   ├── LPM.h       ← misnamed; contains MSTPCR + SCI_t enum
│   ├── RWP.h       ← duplicates SYSC
│   └── SCI.h       ← 70-line flat macro block
└── Source/
    ├── CGC.c       ← EMPTY STUB
    ├── GPIO.c
    ├── IIC.c       ← bypasses LPM abstraction
    ├── LPM.c       ← only covers SCI, not I2C
    ├── RWP.c
    └── SCI.c       ← 300 lines of repeated switch cases
```

### Target Structure (proposed)
```
Driver/
├── Include/
│   ├── drv_common.h   ← SYSC base, common typedefs (replaces SYSC in LPM.h + RWP.h)
│   ├── drv_rwp.h      ← PRCR write-protection API
│   ├── drv_clk.h      ← CGC registers + MSTPCR + clock/module-stop API (replaces CGC.h + LPM.h)
│   ├── drv_gpio.h     ← GPIO (fixed MODE enum, fixed PortToIndex sentinel)
│   ├── drv_uart.h     ← UART: SCI_REG(n, off) macro replaces 70 flat macros; PCLKB correct
│   └── drv_i2c.h      ← I2C: remove dead enums; correct register offsets documented
└── Source/
    ├── drv_rwp.c      ← unchanged logic, new name
    ├── drv_clk.c      ← IMPLEMENT CGC init (PLL or HOCO); MSTPCR unlock for all peripherals
    ├── drv_gpio.c     ← fixed PortToIndex; simplified MODE
    ├── drv_uart.c     ← fixed PCLKB, BRR formula, removed bad SSR clear, safe default in switch
    └── drv_i2c.c      ← fixed init sequence, fixed Start, fixed Receive ACK/NACK
```

---

## Implementation Plan

Each phase is a self-contained commit. Work in order — later phases depend on earlier ones.

---

### Phase 0 — Baseline
**Goal:** Establish a clean starting point before touching functional code.

- [ ] **P0-1** Create `REVIEW_AND_PLAN.md` (this file) in project root
- [ ] **P0-2** Confirm build passes on current code (`cmake --build build/`)
- [ ] **P0-3** Tag current state in git: `git tag pre-fix-baseline`

---

### Phase 1 — Fix Header Dependency & Macro Conflicts
**Goal:** Eliminate BUG-01 and BUG-02. Every header must be self-contained.

- [ ] **P1-1** Create `Driver/Include/drv_common.h`
  - Move `#define SYSC 0x4001E000UL` here (single definition)
  - Add include guard `#ifndef DRV_COMMON_H`
- [ ] **P1-2** Remove `#define SYSC` from `LPM.h`; add `#include "drv_common.h"`
- [ ] **P1-3** Remove `#define SYSC` from `RWP.h`; add `#include "drv_common.h"`
- [ ] **P1-4** Add `#include "drv_common.h"` (or `"LPM.h"`) to `CGC.h`
- [ ] **P1-5** Build → zero warnings about macro redefinition

---

### Phase 2 — Fix UART (SCI) Driver
**Goal:** Fix BUG-03, BUG-04, BUG-05, BUG-09. UART must produce correct baud rates.

- [ ] **P2-1** Verify SEMR bit positions against RA6M5 User Manual §34 (SCI SEMR table)
  - Correct `SEMR_BGDM` and `SEMR_ABCS` defines in `SCI.h`
- [ ] **P2-2** Update `#define PCLKB` to `8000000UL` (MOCO default)
  - Add comment: *"Update this value when CGC is configured for a different PCLKB"*
- [ ] **P2-3** Fix BRR formula in `UART_Init` to match SEMR setting:
  - If keeping ABCS=1, BGDM=1: use `PCLKB / (4 × baudrate) - 1`
  - Alternative: clear SEMR and use standard `PCLKB / (16 × baudrate) - 1`
  - Choose one mode and make formula + SEMR setting consistent
- [ ] **P2-4** Remove the `*SSR &= ~SSR_TDRE` line from `UART_SendChar`
- [ ] **P2-5** Add `default: return;` to all switch statements in `UART_Init`, `UART_SendChar`, `UART_ReceiveChar`
- [ ] **P2-6** Refactor `SCI.h` flat macros: replace 70-line block with single accessor macro
  ```c
  #define SCI_REG8(n, off)  (*(volatile uint8_t *)(SCI_BASE + SCI_STRIDE*(n) + (off)))
  #define SCI_SMR(n)   SCI_REG8(n, 0x00)
  #define SCI_BRR(n)   SCI_REG8(n, 0x01)
  #define SCI_SCR(n)   SCI_REG8(n, 0x02)
  #define SCI_TDR(n)   SCI_REG8(n, 0x03)
  #define SCI_SSR(n)   SCI_REG8(n, 0x04)
  #define SCI_RDR(n)   SCI_REG8(n, 0x05)
  #define SCI_SEMR(n)  SCI_REG8(n, 0x07)
  ```
- [ ] **P2-7** Rewrite `UART_Init`, `UART_SendChar`, `UART_ReceiveChar` to use `SCI_xxx(n)` macros
  - Switch becomes: `case UARTx: n = x; break;` — eliminates 200 lines of repeated pointer assignment
- [ ] **P2-8** Verify UART at 115200 baud on hardware (or logic analyser)

---

### Phase 3 — Fix I2C Driver
**Goal:** Fix BUG-06, BUG-07, BUG-08, BUG-12.

- [ ] **P3-1** Fix `I2C_Init` initialisation sequence (IICRST before ICE)
- [ ] **P3-2** Fix `I2C_Start`: remove the incorrect `while TEND` wait
- [ ] **P3-3** Fix `I2C_Master_Receive_Data` ACK/NACK logic:
  - ACKWP set before ACKBT
  - Last byte → NACK; all others → ACK
  - Check byte index correctly (`i == data_length - 1`)
- [ ] **P3-4** Fix `I2C_Clock_Init`: call through `LPM_Unlock`/`drv_clk` instead of direct MSTPCRB access;
  extend `LPM_Unlock` to accept I2C peripherals
- [ ] **P3-5** Remove unused `I2C_PINCFG_t` and `I2C_ACK_t` enums from `IIC.h`
- [ ] **P3-6** Rename `I2C_LSB_t` enum to `I2C_DIR_t` with values `I2C_WRITE = 0`, `I2C_READ = 1`
  (the name "LSB" is confusing; it controls the R/W direction bit)
- [ ] **P3-7** Verify I2C read/write on hardware with a known slave device (e.g. BME280 or EEPROM)

---

### Phase 4 — Fix GPIO Driver
**Goal:** Fix BUG-10, BUG-11.

- [ ] **P4-1** Change `GPIO_PortToIndex` invalid-port return value from `0` to `0xFF`
- [ ] **P4-2** Add null-check in `GPIO_Config`, `GPIO_Write_Pin`, `GPIO_Read_Pin` for `p == 0xFF`
- [ ] **P4-3** Simplify `GPIO_MODE_t` enum: remove STM32-style speed variants
  ```c
  typedef enum {
      GPIO_MODE_INPUT  = 0,
      GPIO_MODE_OUTPUT = 1
  } GPIO_MODE_t;
  ```
- [ ] **P4-4** Update all call sites of `GPIO_Config` that pass `GPIO_MODE_OUT_10M` → `GPIO_MODE_OUTPUT`
- [ ] **P4-5** Re-run GPIO unit tests; all must pass

---

### Phase 5 — Rename & Reorganize Driver Files
**Goal:** Apply the file reorganization from the plan above. Address S-01 through S-04.

- [ ] **P5-1** Rename `LPM.h` → `drv_clk.h`, `LPM.c` → `drv_clk.c`
  - Rename `LPM_Unlock` → `CLK_ModuleStart`
  - Rename `SCI_t` → `MSTP_Peripheral_t` (or split into `UART_Periph_t` / `I2C_Periph_t`)
- [ ] **P5-2** Rename `RWP.h` → `drv_rwp.h`, `RWP.c` → `drv_rwp.c`
- [ ] **P5-3** Rename `SCI.h` → `drv_uart.h`, `SCI.c` → `drv_uart.c`
- [ ] **P5-4** Rename `IIC.h` → `drv_i2c.h`, `IIC.c` → `drv_i2c.c`
- [ ] **P5-5** Rename `CGC.h` → `drv_clk.h` (merge with step P5-1 result)
- [ ] **P5-6** Update `CMakeLists.txt` and `cmake/GeneratedSrc.cmake` with new file names
- [ ] **P5-7** Update all `#include` directives across `src/` and `Driver/`
- [ ] **P5-8** Fix `main.c` relative includes → use `#include "drv_uart.h"` style (BUG-15)
- [ ] **P5-9** Full rebuild; zero errors and zero warnings

---

### Phase 6 — Implement CGC (Clock) Initialization
**Goal:** Fix S-03. Stop assuming PCLKB; configure it explicitly.

- [ ] **P6-1** Implement `CLK_Init()` in `drv_clk.c`:
  - Option A: Stay on MOCO 8 MHz, set SCKDIVCR for desired PCLKB; update `#define PCLKB`
  - Option B: Switch to HOCO 20/32/48/64 MHz + configure PCLKB divider
  - Option C: Enable PLL for maximum performance (120 MHz ICLK)
  - Document the chosen option and resulting PCLKB value clearly
- [ ] **P6-2** Call `CLK_Init()` from `Reset_Handler` (or `hal_entry`) before any peripheral init
- [ ] **P6-3** Update `#define PCLKB` to match selected configuration exactly

---

### Phase 7 — Add Timeout Protection to Busy-Wait Loops
**Goal:** Fix S-05. Prevent infinite hang on hardware failure.

- [ ] **P7-1** Add timeout counter to `UART_SendChar` and `UART_ReceiveChar`
  - Return an error code (`int8_t`) instead of `void`/`char`; or define a status type
- [ ] **P7-2** Add timeout to all `while()` loops in `I2C_Start`, `I2C_Stop`,
  `I2C_Transmit_Address`, `I2C_Master_Transmit_Data`, `I2C_Master_Receive_Data`
- [ ] **P7-3** Define a timeout threshold constant (e.g. `DRV_TIMEOUT_TICKS`) in `drv_common.h`
- [ ] **P7-4** On timeout: return error code, do NOT leave peripheral in undefined state

---

### Phase 8 — Clean Up src/ Layer
**Goal:** Fix BUG-13, BUG-16; remove dead code.

- [ ] **P8-1** Extract `delay_ms` to `src/utils.h` as `static inline`; remove from both files
- [ ] **P8-2** Fix `test_runner.c`: `g_test_fail_count` increments only once per failed test
  ```c
  void test_record_failure(...) {
      if (!s_current_failed) {   // only count first failure per test
          g_test_fail_count++;
      }
      s_current_failed = 1U;
  }
  ```
- [ ] **P8-3** Decide fate of `main.c`:
  - Either integrate UART self-test into the `hal_entry` test framework
  - Or keep as a standalone alternative entry point with a clear comment
- [ ] **P8-4** Add test stubs for UART and I2C to the test framework

---

### Phase 9 — Final Validation
- [ ] **P9-1** Full clean rebuild: `cmake --build build/ --clean-first`
- [ ] **P9-2** Zero compiler warnings at `-Wall -Wextra`
- [ ] **P9-3** Flash and run hardware tests:
  - LED slow blink = all unit tests pass
  - UART output at correct baud rate verified with logic analyser
  - I2C read/write verified with slave device
- [ ] **P9-4** Update `README.md` with new file structure and correct baud rate information

---

## Summary Table

| Bug ID | File(s) | Severity | Phase |
|--------|---------|----------|-------|
| BUG-01 | `LPM.h`, `RWP.h` | CRITICAL | P1 |
| BUG-02 | `CGC.h` | CRITICAL | P1 |
| BUG-03 | `SCI.h`, `SCI.c` | CRITICAL | P2 |
| BUG-04 | `SCI.c:247` | CRITICAL | P2 |
| BUG-05 | `SCI.c` switch defaults | CRITICAL | P2 |
| BUG-06 | `IIC.c` init sequence | HIGH | P3 |
| BUG-07 | `IIC.c` I2C_Start | HIGH | P3 |
| BUG-08 | `IIC.c` ACK/NACK | HIGH | P3 |
| BUG-09 | `SCI.h` SEMR bits | HIGH | P2 |
| BUG-10 | `GPIO.c` invalid port | MEDIUM | P4 |
| BUG-11 | `GPIO.h` MODE enum | MEDIUM | P4 |
| BUG-12 | `IIC.c` MSTPCR bypass | MEDIUM | P3 |
| BUG-13 | `hal_entry.c`, `main.c` | MEDIUM | P8 |
| BUG-14 | `IIC.h` dead enums | LOW | P3 |
| BUG-15 | `main.c` includes | LOW | P5 |
| BUG-16 | `test_runner.c` counter | LOW | P8 |
| S-01 | `LPM.h` misnamed | STRUCTURAL | P5 |
| S-02 | `SCI.h` flat macros | STRUCTURAL | P2 |
| S-03 | `CGC.c` empty stub | STRUCTURAL | P6 |
| S-04 | SYSC double-defined | STRUCTURAL | P1 |
| S-05 | No timeout in polling | STRUCTURAL | P7 |
| S-06 | No I2C bus recovery | STRUCTURAL | P7 |
