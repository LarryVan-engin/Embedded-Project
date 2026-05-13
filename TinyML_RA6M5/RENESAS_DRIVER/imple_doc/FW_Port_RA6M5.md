# FW_Port_RA6M5

Tags: #done #firmware #rtos #port

Cortex-M33 port layer for the RTOS kernel on RA6M5. Files: `Middleware/Kernel/src/kernel.c` (SysTick init, stack frame builder), `Middleware/Kernel/port/port_cm33.S` (PendSV, SVC). Kernel core: [[FW_Scheduler_Core]]. Context switch details: [[FW_Context_Switch]].

---

## SysTick Configuration

SysTick is the ARM standard system timer in the System Control Space (SCS). Used as the kernel's 1 ms heartbeat.

### Registers

| Register | Address | Purpose |
|----------|---------|---------|
| SYST_CSR | 0xE000E010 | Control and Status |
| SYST_RVR | 0xE000E014 | Reload Value |
| SYST_CVR | 0xE000E018 | Current Value |

### SYST_CSR Bit Fields

| Bit | Name | Value | Meaning |
|-----|------|-------|---------|
| 0 | ENABLE | 1 | Counter enabled |
| 1 | TICKINT | 1 | Exception on count-to-zero |
| 2 | CLKSOURCE | 1 | Processor clock (ICLK = 200 MHz) |
| 16 | COUNTFLAG | RO | Set when counter reaches zero |

### Timing Calculation

```
ICLK = 200 MHz (PLL)
Desired tick = 1 ms = 1000 Hz

RVR = (ICLK / tick_rate) − 1
    = (200,000,000 / 1,000) − 1
    = 199,999

Period = (199,999 + 1) × (1 / 200,000,000) = 0.001 s = 1 ms  ✓
```

### Init Code

```c
SYST_RVR = 199999U;   /* Reload value for 1 ms */
SYST_CVR = 0U;        /* Clear current value   */
SYST_CSR = 0x07U;     /* ENABLE | TICKINT | CLKSOURCE */
```

---

## NVIC Priority Setup

### SHPR3 Register (0xE000ED20)

| Bits | Field | Value | Rationale |
|------|-------|-------|-----------|
| [23:16] | PendSV priority | 0xFF | Lowest priority — context switches never preempt ISRs |
| [31:24] | SysTick priority | 0xFF | Lowest priority — scheduler runs after all ISR work |

```c
SCB_SHPR3 = 0xFFFF0000UL;  /* PendSV=0xFF, SysTick=0xFF */
```

Why lowest priority? PendSV implements context switching. If PendSV could preempt other ISRs, we'd risk corrupting interrupt state. By running PendSV at the lowest level, the context switch is deferred until all ISRs complete.

---

## Initial Stack Frame Layout

Built by `os_stack_init()` in `kernel.c`. Creates a fake exception-return frame so that the first PendSV/SVC pops registers and jumps to the task entry point.

```
[SP after init] ──► R4          ┐
                    R5          │
                    R6          │
                    R7          ├── Software-saved (PendSV)
                    R8          │   9 words = 36 bytes
                    R9          │
                    R10         │
                    R11         │
                    LR(EXC_RET) ┘  = 0xFFFFFFFD
                    ─────────────────────────────
                    R0 (= arg)  ┐
                    R1          │
                    R2          │
                    R3          ├── Hardware-stacked
                    R12         │   8 words = 32 bytes
                    LR (trap)   │
                    PC (entry)  │
                    xPSR        ┘  = 0x01000000
[Top of stack] ──►
```

---

## EXC_RETURN Values (Cortex-M33)

| Value | Meaning |
|-------|---------|
| 0xFFFFFFF1 | Return to Handler mode, MSP |
| 0xFFFFFFF9 | Return to Thread mode, MSP, no FPU |
| 0xFFFFFFFD | Return to Thread mode, PSP, no FPU |
| 0xFFFFFFE1 | Return to Handler mode, MSP, with FPU |
| 0xFFFFFFE9 | Return to Thread mode, MSP, with FPU |
| 0xFFFFFFED | Return to Thread mode, PSP, with FPU |

Bit 4: 1 = standard frame (no FPU), 0 = extended frame (FPU context stacked).

Tasks start with `EXC_RETURN = 0xFFFFFFFD` (Thread mode, PSP, no FPU). If a task uses FPU instructions, hardware changes the stacking behavior, and subsequent PendSV will detect bit 4 = 0 and save S16-S31. See [[FW_Context_Switch]].

---

## CONTROL Register

| Bit | Name | Description |
|-----|------|-------------|
| 0 | nPRIV | 0 = privileged, 1 = unprivileged |
| 1 | SPSEL | 0 = MSP, 1 = PSP in Thread mode |
| 2 | FPCA | FP context active (auto-managed) |

The SVC_Handler sets `CONTROL = 0x02` (SPSEL = 1, privileged) so that all tasks use PSP. MSP is reserved for exception handlers.

---

## Related Notes

- [[FW_Scheduler_Core]] — TCB, ready list, bitmap algorithm
- [[FW_Context_Switch]] — PendSV assembly walkthrough
- [[HW_RA6M5_ClockTree]] — ICLK 200 MHz via PLL clock tree
