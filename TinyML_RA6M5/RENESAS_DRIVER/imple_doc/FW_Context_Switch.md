# FW_Context_Switch

Tags: #done #firmware #rtos #assembly

PendSV-based context switching for Cortex-M33 with FPU support. File: `Middleware/Kernel/port/port_cm33.S`. Kernel core: [[FW_Scheduler_Core]]. Port config: [[FW_Port_RA6M5]].

---

## Why PendSV (Not SVC)?

| Mechanism | Use Case | Problem for Context Switching |
|-----------|----------|-------------------------------|
| SVC | Synchronous system calls | Cannot be issued from within an ISR |
| PendSV | Deferred exception | Can be triggered from any context |

Context switches often need to happen from within SysTick_Handler (an ISR). SVC cannot be called from Handler mode (causes a HardFault). PendSV is designed specifically for this: it is pended (PENDSVSET in ICSR) and executes later, at the lowest priority, after all other ISRs complete.

---

## PendSV_Handler — Step-by-Step

### Step 1: Disable Interrupts

```arm
cpsid   i       /* Set PRIMASK — mask all configurable-priority IRQs */
isb             /* Instruction Synchronization Barrier */
```

Prevents re-entrant PendSV or any interrupt that could corrupt the context switch.

### Step 2: Read PSP

```arm
mrs     r0, psp
```

After exception entry, PSP points just past the hardware-stacked frame (R0-R3, R12, LR, PC, xPSR). If FPU was used, S0-S15 + FPSCR are also stacked by hardware.

### Step 3: FPU Context Save (Conditional)

```arm
tst     lr, #0x10       /* Test EXC_RETURN bit 4 */
it      eq
vstmdbeq r0!, {s16-s31} /* If bit4=0 → FPU used → save S16-S31 */
```

- **Bit 4 = 1**: Standard frame. No FPU context to save. S0-S15 not stacked.
- **Bit 4 = 0**: Extended frame. Hardware already stacked S0-S15 + FPSCR. We must save S16-S31 manually (callee-saved, 16 regs × 4 bytes = 64 bytes).

This is critical for TinyML/AI workloads where FP register corruption destroys model accuracy.

### Step 4: Save R4-R11, LR

```arm
stmdb   r0!, {r4-r11, lr}
```

STMDB (Decrement Before): stores 9 registers (36 bytes) below current R0. R4 at the lowest address, LR (EXC_RETURN) at the highest. R0 updated to new stack top.

### Step 5: Store SP to Outgoing TCB

```arm
ldr     r1, =os_current_task
ldr     r2, [r1]        /* R2 = os_current_task pointer */
str     r0, [r2, #0]    /* TCB->sp = R0 (offset 0) */
```

### Step 6: Switch TCB Pointers

```arm
ldr     r3, =os_next_task
ldr     r2, [r3]        /* R2 = os_next_task */
str     r2, [r1]        /* os_current_task = os_next_task */
```

### Step 7-8: Restore Incoming Context

```arm
ldr     r0, [r2, #0]            /* R0 = os_next_task->sp */
ldmia   r0!, {r4-r11, lr}       /* Pop R4-R11, LR(EXC_RETURN) */
```

### Step 9: FPU Context Restore (Conditional)

```arm
tst     lr, #0x10
it      eq
vldmiaeq r0!, {s16-s31}         /* If bit4=0 → restore S16-S31 */
```

### Step 10-11: Set PSP and Return

```arm
msr     psp, r0         /* Point PSP to hardware frame */
cpsie   i               /* Re-enable interrupts */
isb
bx      lr              /* Exception return → hardware pops R0-R3, R12, LR, PC, xPSR */
```

---

## Stack Layout During Context Switch

### Outgoing Task (after PendSV save)

```
TCB->sp ──► R4          ┐
            R5          │
            R6          │
            R7          │  Software-saved
            R8          │  (9 words)
            R9          │
            R10         │
            R11         │
            LR(EXC_RET) ┘
            ─────────────
            S16         ┐  (only if FPU used)
            S17         │
            ...         │  16 × 4 = 64 bytes
            S31         ┘
            ─────────────
            R0          ┐
            R1          │
            R2          │  Hardware-stacked
            R3          │  (8 or 26 words)
            R12         │
            LR          │
            PC          │
            xPSR        ┘
            [S0-S15, FPSCR]  (only if FPU used)
```

---

## SVC_Handler — First Task Launch

Used only once, from `OS_Start()` → `OS_StartFirstTask()`.

1. Loads `os_current_task->sp`.
2. Pops R4-R11, LR(EXC_RETURN) via LDMIA.
3. Sets PSP to the remaining hardware frame.
4. Writes `CONTROL = 0x02` → Thread mode uses PSP.
5. `BX LR` → exception return → task begins.

After this, all subsequent switches are handled by PendSV.

---

## Critical Section Handling

PendSV uses `CPSID I` / `CPSIE I` to bracket the context switch. This prevents:
- **Nested PendSV**: if a higher-priority ISR fires during PendSV and triggers another PendSV, the TCB pointers could be corrupted.
- **Interrupted save/restore**: partial register save → task resumes with wrong state.

The `ISB` after `CPSID/CPSIE` flushes the pipeline, ensuring the mask takes effect before the next instruction.

---

## Related Notes

- [[FW_Scheduler_Core]] — TCB structure, ready list, bitmap algorithm
- [[FW_Port_RA6M5]] — SysTick, NVIC, EXC_RETURN encoding
