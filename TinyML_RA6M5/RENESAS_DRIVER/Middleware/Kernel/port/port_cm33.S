/**
 * @file    port_cm33.S
 * @brief   Cortex-M33 CPU Port — PendSV Context Switch & First-Task Launch
 *
 * Target:  Renesas RA6M5 (R7FA6M5BH, ARM Cortex-M33, FPv5-SP-D16)
 * Toolchain: GCC ARM (GNU as syntax)
 *
 * This file provides two critical functions:
 *
 *   1. PendSV_Handler — Performs the actual context switch between tasks.
 *      Saves R4-R11, LR(EXC_RETURN) of the outgoing task onto its stack,
 *      and restores them for the incoming task.  Handles FPU S16-S31
 *      save/restore when the task used floating-point instructions.
 *
 *   2. OS_StartFirstTask — Launched via SVC from OS_Start().
 *      Loads the first task's context and performs an exception return
 *      to begin execution in Thread mode using PSP.
 *
 * ======================================================================
 * CRITICAL INVARIANT:
 *   os_current_task->sp  MUST be at offset 0 in OS_TCB_t.
 *   If the struct layout changes, this assembly WILL break.
 * ======================================================================
 *
 * FPU Context Switching Strategy:
 *
 *   Cortex-M33 with FPv5 uses LAZY STACKING (FPCCR.LSPEN = 1 by default):
 *     - On exception entry, if the FPU was used, hardware RESERVES space
 *       on the stack for S0-S15 + FPSCR but does NOT actually write them
 *       until a new FPU instruction is executed in the handler.
 *     - EXC_RETURN bit 4 indicates the frame type:
 *         bit4 = 1 → Standard frame (8 words, no FPU)
 *         bit4 = 0 → Extended frame (26 words, S0-S15 + FPSCR included)
 *     - S0-S15 are caller-saved (handled by hardware).
 *     - S16-S31 are callee-saved — we must save/restore them manually.
 *
 *   This handler checks EXC_RETURN[4] and conditionally saves/restores
 *   S16-S31 (16 × 4 = 64 bytes) only when the FPU was actually used.
 *   This is critical for TinyML/AI workloads that rely on FP accuracy.
 */

    .syntax unified
    .cpu    cortex-m33
    .fpu    fpv5-sp-d16
    .thumb

/* ======================================================================
 * External symbols — defined in kernel.c
 * ====================================================================== */
    .extern os_current_task     /* OS_TCB_t *volatile — current TCB ptr  */
    .extern os_next_task        /* OS_TCB_t *volatile — next TCB ptr     */

/* ======================================================================
 * PendSV_Handler — Context Switch
 *
 * Called by the NVIC when PENDSVSET is written in ICSR.
 * Runs at the LOWEST interrupt priority (0xFF in SHPR3) so it only
 * executes when no other ISR is active.
 *
 * Register usage:
 *   R0 = stack pointer being manipulated
 *   R1 = address of os_current_task
 *   R2 = TCB pointer (current, then next)
 *   R3 = address of os_next_task
 *   LR = EXC_RETURN value (encodes return state)
 *
 * Stack state on entry (hardware has already pushed):
 *   [PSP] → R0, R1, R2, R3, R12, LR, PC, xPSR
 *            (+ S0-S15, FPSCR if FPU was used)
 * ====================================================================== */

    .section .text.PendSV_Handler, "ax", %progbits
    .global  PendSV_Handler
    .type    PendSV_Handler, %function
    .align   2
    .thumb_func

PendSV_Handler:
    /* ---------------------------------------------------------------
     * Step 1: Disable interrupts to prevent re-entrant PendSV.
     * CPSID I sets PRIMASK, masking all configurable-priority IRQs.
     * --------------------------------------------------------------- */
    cpsid   i
    isb

    /* ---------------------------------------------------------------
     * Step 2: Read the Process Stack Pointer of the outgoing task.
     * After exception entry, PSP points just past the auto-stacked
     * hardware frame (R0-R3, R12, LR, PC, xPSR [, S0-S15, FPSCR]).
     * --------------------------------------------------------------- */
    mrs     r0, psp

    /* ---------------------------------------------------------------
     * Step 3: Check if the outgoing task used the FPU.
     * EXC_RETURN is in LR.  Bit 4:
     *   1 = standard frame (no FPU) → skip S16-S31 save
     *   0 = extended frame (FPU)    → save S16-S31
     *
     * VSTMDB stores S16-S31 onto the task stack (16 regs × 4 bytes).
     * --------------------------------------------------------------- */
    tst     lr, #0x10
    it      eq
    vstmdbeq r0!, {s16-s31}

    /* ---------------------------------------------------------------
     * Step 4: Save callee-saved registers R4-R11 and LR (EXC_RETURN)
     * onto the outgoing task's stack.
     *
     * STMDB (Store Multiple, Decrement Before):
     *   Stores 9 words (36 bytes) below current R0.
     *   Memory layout (ascending address):
     *     R0-36 → R4
     *     R0-32 → R5
     *     R0-28 → R6
     *     R0-24 → R7
     *     R0-20 → R8
     *     R0-16 → R9
     *     R0-12 → R10
     *     R0-8  → R11
     *     R0-4  → LR (EXC_RETURN)
     *   R0 is updated to R0-36 (writeback via !).
     * --------------------------------------------------------------- */
    stmdb   r0!, {r4-r11, lr}

    /* ---------------------------------------------------------------
     * Step 5: Save the updated stack pointer into the outgoing TCB.
     * os_current_task->sp = R0
     * (sp is at offset 0 in OS_TCB_t, so STR to [R2] is correct.)
     * --------------------------------------------------------------- */
    ldr     r1, =os_current_task
    ldr     r2, [r1]            /* R2 = os_current_task             */
    str     r0, [r2, #0]        /* os_current_task->sp = R0         */

    /* ---------------------------------------------------------------
     * Step 6: Switch to the incoming task.
     * os_current_task = os_next_task
     * --------------------------------------------------------------- */
    ldr     r3, =os_next_task
    ldr     r2, [r3]            /* R2 = os_next_task                */
    str     r2, [r1]            /* os_current_task = os_next_task   */

    /* ---------------------------------------------------------------
     * Step 7: Load the incoming task's stack pointer from its TCB.
     * R0 = os_next_task->sp
     * --------------------------------------------------------------- */
    ldr     r0, [r2, #0]        /* R0 = os_next_task->sp            */

    /* ---------------------------------------------------------------
     * Step 8: Restore R4-R11 and LR (EXC_RETURN) from the incoming
     * task's stack.
     *
     * LDMIA (Load Multiple, Increment After):
     *   Loads 9 words starting at R0.
     *   R4 from lowest address, LR from highest.
     *   R0 updated to point past the loaded region.
     * --------------------------------------------------------------- */
    ldmia   r0!, {r4-r11, lr}

    /* ---------------------------------------------------------------
     * Step 9: Check if the incoming task has FPU context to restore.
     * Same EXC_RETURN bit 4 check — the LR just loaded from the
     * incoming task's stack carries its original EXC_RETURN value.
     * --------------------------------------------------------------- */
    tst     lr, #0x10
    it      eq
    vldmiaeq r0!, {s16-s31}

    /* ---------------------------------------------------------------
     * Step 10: Set PSP to the incoming task's adjusted stack pointer.
     * On exception return, hardware will pop R0-R3, R12, LR, PC,
     * xPSR (and S0-S15, FPSCR if extended frame) from this PSP.
     * --------------------------------------------------------------- */
    msr     psp, r0

    /* ---------------------------------------------------------------
     * Step 11: Re-enable interrupts and return.
     * BX LR performs the exception return.  The hardware:
     *   - Pops the exception frame from PSP
     *   - Restores R0-R3, R12, LR, PC, xPSR
     *   - Jumps to the restored PC (incoming task resumes)
     * --------------------------------------------------------------- */
    cpsie   i
    isb
    bx      lr

    .size PendSV_Handler, . - PendSV_Handler

/* ======================================================================
 * OS_StartFirstTask — Launch the initial task via SVC
 *
 * Called once from OS_Start() in kernel.c.  Issues an SVC instruction
 * to transition into Handler mode, from which we can perform a proper
 * exception return into the first task.
 *
 * Why SVC?
 *   We cannot use BX with an EXC_RETURN value from Thread mode.
 *   SVC puts us in Handler mode.  The SVC_Handler then loads the
 *   first task's context and does BX LR to "return" into the task.
 * ====================================================================== */

    .section .text.OS_StartFirstTask, "ax", %progbits
    .global  OS_StartFirstTask
    .type    OS_StartFirstTask, %function
    .align   2
    .thumb_func

OS_StartFirstTask:
    /* Trigger SVC exception — number is ignored, we only have one use. */
    svc     #0
    /* Never returns.  Flow continues in SVC_Handler below. */
    nop

    .size OS_StartFirstTask, . - OS_StartFirstTask

/* ======================================================================
 * SVC_Handler — First-Task Launcher
 *
 * Used exclusively by OS_StartFirstTask to boot the first task.
 * This handler:
 *   1. Loads the first task's saved SP from os_current_task->sp
 *   2. Pops R4-R11, LR(EXC_RETURN) from the task's stack
 *   3. Sets PSP to point to the hardware exception frame
 *   4. Switches CONTROL register to use PSP in Thread mode
 *   5. Performs exception return (BX LR) → task starts executing
 *
 * After this point, all context switches are handled by PendSV.
 * ====================================================================== */

    .section .text.SVC_Handler, "ax", %progbits
    .global  SVC_Handler
    .type    SVC_Handler, %function
    .align   2
    .thumb_func

SVC_Handler:
    /* Load os_current_task pointer. */
    ldr     r0, =os_current_task
    ldr     r1, [r0]            /* R1 = os_current_task (TCB pointer)  */
    ldr     r0, [r1, #0]        /* R0 = os_current_task->sp            */

    /* Pop the software-saved frame: R4-R11, LR(EXC_RETURN) */
    ldmia   r0!, {r4-r11, lr}

    /* Set PSP — hardware exception return will pop from here. */
    msr     psp, r0

    /* Switch Thread mode to use PSP (CONTROL.SPSEL = 1).
     * Bit 1 of CONTROL:
     *   0 = use MSP in Thread mode (default after reset)
     *   1 = use PSP in Thread mode
     * After this, MSP is used only by exception handlers. */
    mov     r0, #2
    msr     control, r0
    isb                         /* Flush pipeline after CONTROL write. */

    /* Exception return — hardware pops R0-R3, R12, LR, PC, xPSR
     * from PSP and jumps to the task's entry point (PC). */
    bx      lr

    .size SVC_Handler, . - SVC_Handler

/* ======================================================================
 * End of file
 * ====================================================================== */
    .end
