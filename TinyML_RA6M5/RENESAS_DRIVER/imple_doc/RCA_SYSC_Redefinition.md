# RCA_SYSC_Redefinition

Tags: #done #firmware #system

Root cause analysis for the `SYSC` macro redefinition link error. Affected modules: [[FW_Clock_Driver]], [[FW_RWP_Driver]].

---

## Symptoms

- Linker error: `SYSC` redefined — multiple definition across translation units
- Any project including both `LPM.h` and `RWP.h` failed to compile

---

## Hardware Root Cause

`SYSC = 0x4001E000` is the base address of the System Control block. It is required by every subsystem that touches clocks, module stop, or write protection. There is no hardware reason for it to live in any specific driver file; it is a shared constant.

---

## Root Cause in Code

Both `LPM.h` and `RWP.h` independently defined:

```c
#define SYSC  0x4001E000UL
```

When any `.c` file included both headers (e.g., `IIC.c` included both `LPM.h` and `RWP.h`), the preprocessor emitted a duplicate macro definition warning. In pedantic-error mode this became a compile error.

Additionally, `CGC.h` used `SYSC` in its register macros without including the header that defined it — a missing dependency.

---

## Fix

Created `Driver/Include/drv_common.h` as the single source of truth:

```c
#define SYSC  0x4001E000UL
```

All driver headers now include `drv_common.h` instead of re-defining `SYSC`. `CGC.h` dependency was resolved in the same change.

---

## Files Changed

- `drv_common.h` — created, defines SYSC (and later DRV_TIMEOUT_TICKS, drv_status_t)
- `LPM.h` → `drv_clk.h` — SYSC definition removed
- `RWP.h` → `drv_rwp.h` — SYSC definition removed
- `CGC.h` — added `#include "drv_common.h"`
