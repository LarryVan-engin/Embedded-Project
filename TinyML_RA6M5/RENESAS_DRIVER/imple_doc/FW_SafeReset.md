# FW_SafeReset

Tags: #firmware #safety #nvs #stack

Safe software-reset module for RA6M5 with NVS crash-reason persistence and
task stack-overflow detection.

Files: `src/safe_reset.h`, `src/safe_reset.c`.

---

## Purpose

| Problem | Solution |
|---------|----------|
| Unhandled fault → MCU resets, no crash info | Write reason to Data Flash NVS **before** reset |
| Crash record survives → infinite reset loop | `safe_reset_init()` clears marker on first read |
| Stack overflow undetected until code corruption | Canary word planted by kernel, checked periodically |
| Hard to know which task overflowed | `OS_StackOverflowCheck()` returns 1-based task index |

---

## NVS Crash Record

Stored in the **last** 64-byte Data Flash block (`DATA_FLASH_LAST_BLOCK_ADDR` = `0x08001FC0`).
Bytes `[0..11]` are owned by the OTA FWUpdate path and are preserved untouched.

```
Offset  Size  Content
------  ----  -------
12..15   4    Crash magic 0xC0 0xFF 0xEE 0x00  (written by safe_reset_trigger)
16..19   4    Reset reason code  (big-endian uint32_t)
20..23   4    Cumulative reset count (big-endian uint32_t, persists across boots)
```

Magic bytes are set to `0xFF` (blank) when no crash record is pending.

---

## Reset Reason Codes

| Constant | Value | Meaning |
|----------|-------|---------|
| `SAFE_RESET_REASON_NONE` | `0x00000000` | Clean boot, no crash |
| `SAFE_RESET_REASON_STACK_OVERFLOW` | `0x00000001` | Task stack canary corrupted |
| `SAFE_RESET_REASON_FAULT` | `0x00000002` | HardFault / application fatal error |
| `SAFE_RESET_REASON_WATCHDOG` | `0x00000003` | Application watchdog expired |
| `SAFE_RESET_REASON_APP_REQUEST` | `0x00000004` | Deliberate reset (e.g. self-test) |

---

## API

### `safe_reset_init()`

```c
uint32_t safe_reset_init(void);
```

- Must be called **first** in `hal_entry()`, before any task creation.
- Reads the NVS block (memory-mapped, no P/E mode required).
- If crash magic `0xC0FFEE00` is found at bytes `[12..15]`:
  - Logs reason + count via `debug_print()`.
  - Clears bytes `[12..15]` → `0xFF`, preserves `[16..23]` count for reference.
  - Enters P/E, erases block, writes back modified 64 bytes, exits P/E.
  - Returns the reason code.
- Otherwise returns `SAFE_RESET_REASON_NONE`.

**Critical**: Consuming the marker before any potentially faulty code runs
prevents an infinite reset loop.

### `safe_reset_trigger()`

```c
void safe_reset_trigger(uint32_t reason);  /* never returns */
```

- Reads existing NVS block.
- Writes crash magic + reason + increments count in buffer.
- Enters P/E, erases block, writes 64 bytes back, exits P/E.
- Triggers `SCB->AIRCR = VECTKEY | SYSRESETREQ` — hardware reset.

Even if the flash write fails the reset is still triggered; crash reason
may not survive in that edge case.

### `safe_reset_check_stacks()`

```c
void safe_reset_check_stacks(void);
```

- Calls `OS_StackOverflowCheck()` (kernel API).
- If any task canary is corrupted, calls
  `safe_reset_trigger(SAFE_RESET_REASON_STACK_OVERFLOW)`.
- Called from `task_led_blink` in `main.c` every 250 ms.

---

## Stack Canary Mechanism

The custom RTOS kernel plants `OS_STACK_CANARY_VALUE` (`0xDEADC0DE`) at
`tcb->stack[0]` (lowest address of the stack array) inside `os_stack_init()`
during `OS_Task_Create()`.

The stack grows **downward** from `tcb->stack[OS_DEFAULT_STACK_WORDS]`, so
`stack[0]` is the first word overwritten when the stack overflows.

`OS_StackOverflowCheck()` (in `kernel.c`) iterates all registered tasks and
returns the 1-based index of the first task with a corrupted canary.

Stack size was increased from 4 KB to **8 KB** (`OS_DEFAULT_STACK_SIZE=8192`)
to provide adequate headroom for TFLite Micro `Invoke()` call-chain depth.

---

## Self-Test Flow

Controlled by `#define SAFE_RESET_SELF_TEST 1` in `hal_entry.c`.

```
Boot 1 (clean NVS, no crash marker):
  safe_reset_init()  → returns SAFE_RESET_REASON_NONE
  (SELF_TEST gate)   → calls safe_reset_trigger(APP_REQUEST)
                         writes NVS: magic=0xC0FFEE00, reason=0x00000004, count=1
                         SCB->AIRCR → hardware reset

Boot 2 (crash marker in NVS):
  safe_reset_init()  → finds magic, logs "reason=0x00000004 count=1"
                         clears magic bytes → 0xFF
                         returns SAFE_RESET_REASON_APP_REQUEST
  (SELF_TEST gate)   → logs "Recovered from reason=0x00000004, continuing."
                         does NOT trigger another reset ✓
  Normal execution continues.
```

---

## Integration Points

| File | Change |
|------|--------|
| `src/hal_entry.c` | `#include "safe_reset.h"`, calls `safe_reset_init()` first thing |
| `src/main.c` | `#include "safe_reset.h"`, `safe_reset_check_stacks()` in `task_led_blink` every 250 ms |
| `Middleware/Kernel/src/kernel.c` | `os_stack_init()` writes canary to `tcb->stack[0]`; adds `OS_StackOverflowCheck()` |
| `Middleware/Kernel/include/kernel.h` | Declares `OS_STACK_CANARY_VALUE` + `OS_StackOverflowCheck()` |
| `Config/rtos_config.h` | `OS_DEFAULT_STACK_SIZE` raised from 4096 to 8192 |

---

## Hardware Reset Method

```c
/* SCB Application Interrupt and Reset Control Register */
#define SCB_AIRCR  (*(volatile uint32_t *)0xE000ED0CUL)
/* VECTKEY=0x05FA, SYSRESETREQ=bit2 */
SCB_AIRCR = (0x05FAUL << 16U) | (1UL << 2U);
```

Cortex-M33 asserts SYSRESETREQ → on-chip reset controller generates a
full system reset (all peripherals, CPU, SRAM).  Data Flash contents survive.
