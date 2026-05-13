# FW_Scheduler_Core

Tags: #done #firmware #rtos #kernel

Preemptive RTOS kernel for RA6M5 (Cortex-M33). Files: `Middleware/Kernel/kernel.h`, `Middleware/Kernel/src/kernel.c`. Port layer: [[FW_Port_RA6M5]]. Context switch: [[FW_Context_Switch]].

---

## Architecture

The kernel implements **priority-based preemptive scheduling** with **Round-Robin** for tasks sharing the same priority level. All memory is statically allocated — no `malloc` or `new`.

| Parameter | Value |
|-----------|-------|
| Max tasks | 32 |
| Priority levels | 32 (0 = highest, 31 = lowest) |
| Stack per task | 4096 bytes (1024 words) |
| Tick rate | 1 ms (SysTick at 200 MHz ICLK) |

---

## Task Control Block (TCB)

```c
typedef struct OS_TCB {
    uint32_t      *sp;          /* Saved stack pointer — MUST be offset 0 */
    struct OS_TCB *next;        /* Round-robin circular linked list       */
    uint32_t       priority;    /* 0 = highest, 31 = lowest               */
    OS_TaskState_t state;       /* READY / RUNNING / BLOCKED / SUSPENDED  */
    uint32_t       delay_ticks; /* Remaining ticks for OS_Task_Delay()    */
    const char    *name;        /* Debug identifier                       */
    uint32_t       stack[1024]; /* Static stack storage (4 KB)            */
} OS_TCB_t;
```

**Critical constraint**: `sp` at offset 0 is required by the PendSV assembly in [[FW_Context_Switch]]. Changing the struct layout breaks context switching.

---

## Priority Bitmap — O(1) Scheduling

A single `uint32_t os_ready_bitmap` encodes which priority levels have ready tasks.

**Encoding**: bit position `(31 − priority)` is set when at least one task at that priority is ready.

**Lookup**: `highest_ready_priority = __CLZ(os_ready_bitmap)` — the ARM Count Leading Zeros intrinsic gives the highest-priority non-empty level in a single cycle.

| Priority | Bitmap bit | `__CLZ` result |
|----------|-----------|----------------|
| 0 (highest) | bit 31 | 0 |
| 1 | bit 30 | 1 |
| 31 (lowest) | bit 0 | 31 |

---

## Round-Robin Ready Lists

Each priority level has a **circular singly-linked list** of TCBs via the `next` pointer.

- **Insert**: new task added after the current head (runs last — fairness).
- **Remove**: predecessor found via traversal (O(n) at that level, bounded by 32 tasks total).
- **Rotate**: on each SysTick, the head is advanced: `os_ready_list[prio] = os_ready_list[prio]->next`.

When a list becomes empty, the bitmap bit is cleared. When a task is added to an empty list, the bit is set.

---

## Task State Machine

```
                OS_Task_Create()
                     │
                     ▼
    ┌──────────── READY ◄──────────────┐
    │                │                 │
    │   OS_Schedule()│    delay_ticks  │
    │                │    expires in   │
    │                ▼    SysTick_Handler
    │            RUNNING ──────────────┘
    │                │
    │   OS_Task_Delay()
    │                │
    │                ▼
    └─────────── BLOCKED
```

---

## OS_Task_Delay()

1. Sets `delay_ticks` on the current TCB.
2. Changes state to `BLOCKED`.
3. Removes from the ready list (clears bitmap bit if last at that priority).
4. Calls `OS_Schedule()` → triggers PendSV for context switch.

In `SysTick_Handler` (every 1 ms):
- All blocked tasks are scanned.
- `delay_ticks` is decremented.
- When it reaches 0, the task is moved back to READY and re-inserted into the ready list.

---

## API Summary

| Function | Description |
|----------|-------------|
| `OS_Init()` | Zero state, create idle task |
| `OS_Task_Create()` | Build stack frame, add to ready list |
| `OS_Start()` | Configure SysTick, launch first task via SVC |
| `OS_Task_Delay(ticks)` | Block calling task for N milliseconds |
| `OS_Yield()` | Voluntarily give up remaining time-slice |
| `OS_Schedule()` | Bitmap lookup + PendSV trigger |
| `OS_GetTick()` | Read current tick count |
| `OS_EnterCritical()` | CPSID I with nesting support |
| `OS_ExitCritical()` | CPSIE I when nesting reaches 0 |

---

## Related Notes

- [[FW_Port_RA6M5]] — SysTick config, NVIC priorities, stack frame init
- [[FW_Context_Switch]] — PendSV assembly, FPU context, register save/restore
- [[FW_Semaphore]] — Counting/binary semaphores with timeout
- [[FW_Software_Timer]] — One-shot/auto-reload timers, daemon task
- [[HW_RA6M5_ClockTree]] — ICLK 200 MHz PLL clock tree

