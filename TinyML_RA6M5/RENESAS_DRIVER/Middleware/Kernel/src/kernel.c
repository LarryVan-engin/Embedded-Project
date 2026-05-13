/**
 * @file    kernel.c
 * @brief   Preemptive RTOS Kernel — Core Implementation
 *
 * O(1) bitmap scheduler, per-priority round-robin, tick-based blocking,
 * semaphore timeout integration, and software timer tick dispatch.
 *
 * Target: RA6M5 (Cortex-M33), ICLK = 200 MHz (or 48 MHz fallback), 1 ms tick.
 */

#include "kernel.h"
#include "semaphore.h"
#include "software_timer.h"
#include "drv_clk.h"
#include <string.h>

/* ======================================================================
 * SCS Register Definitions (ARM System Control Space)
 * ====================================================================== */

/** SysTick Control and Status.  Bit0=ENABLE, Bit1=TICKINT, Bit2=CLKSOURCE. */
#define SYST_CSR (*(volatile uint32_t *)0xE000E010UL)
/** SysTick Reload Value.  Bits[23:0] = reload count. */
#define SYST_RVR (*(volatile uint32_t *)0xE000E014UL)
/** SysTick Current Value.  Any write clears counter + COUNTFLAG. */
#define SYST_CVR (*(volatile uint32_t *)0xE000E018UL)

#define SYST_CSR_ENABLE (1UL << 0)
#define SYST_CSR_TICKINT (1UL << 1)
#define SYST_CSR_CLKSOURCE (1UL << 2)

/** ICSR — Bit 28 = PENDSVSET: writing 1 triggers PendSV. */
#define SCB_ICSR (*(volatile uint32_t *)0xE000ED04UL)
#define ICSR_PENDSVSET (1UL << 28)

/** SHPR3 — Bits[23:16]=PendSV prio, Bits[31:24]=SysTick prio. */
#define SCB_SHPR3 (*(volatile uint32_t *)0xE000ED20UL)

/* Stack frame constants */
#define XPSR_INIT_VALUE 0x01000000UL       /* Thumb bit */
#define EXC_RETURN_THREAD_PSP 0xFFFFFFFDUL /* Thread, PSP, no FPU */

/* ======================================================================
 * Kernel State
 * ====================================================================== */

OS_TCB_t *volatile os_current_task;
OS_TCB_t *volatile os_next_task;

static volatile uint32_t os_tick_count;
volatile uint32_t os_ready_bitmap;          /* extern'd by semaphore.c */
OS_TCB_t *os_ready_list[OS_MAX_PRIORITIES]; /* extern'd by semaphore.c */
static OS_TCB_t *os_task_table[OS_MAX_TASKS];
static uint32_t os_task_count;
static volatile uint32_t os_critical_nesting;

/* Idle task — lowest priority */
static OS_TCB_t os_idle_tcb;

/* ======================================================================
 * Internal Helpers
 * ====================================================================== */

void OS_ReadyListInsert(OS_TCB_t *tcb);
void OS_ReadyListRemove(OS_TCB_t *tcb);
void OS_ReadyListRotate(uint32_t prio);
void SysTick_Handler(void);

static void os_task_exit_error(void) {
  __asm volatile("cpsid i");
  for (;;) {
    __asm volatile("nop");
  }
}

static void os_idle_task(void *arg) {
  (void)arg;
  for (;;) {
    __asm volatile("wfi");
  }
}

/** Build a fake exception-return stack frame for a new task.
 *  Also plants a canary at the lowest stack address to detect overflow. */
static uint32_t *os_stack_init(OS_TCB_t *tcb, void (*entry)(void *),
                               void *arg) {
  /* Plant canary at the bottom of the stack (lowest address). */
  tcb->stack[0] = OS_STACK_CANARY_VALUE;

  uint32_t *sp = &tcb->stack[OS_DEFAULT_STACK_WORDS];

  /* Hardware-stacked frame (popped by exception return) */
  *(--sp) = XPSR_INIT_VALUE;
  *(--sp) = (uint32_t)(uintptr_t)entry;
  *(--sp) = (uint32_t)(uintptr_t)&os_task_exit_error;
  *(--sp) = 0U;                       /* R12 */
  *(--sp) = 0U;                       /* R3  */
  *(--sp) = 0U;                       /* R2  */
  *(--sp) = 0U;                       /* R1  */
  *(--sp) = (uint32_t)(uintptr_t)arg; /* R0 */

  /* Software-saved frame (popped by PendSV LDMIA) */
  *(--sp) = EXC_RETURN_THREAD_PSP; /* LR (EXC_RETURN) */
  *(--sp) = 0U;                    /* R11 */
  *(--sp) = 0U;                    /* R10 */
  *(--sp) = 0U;                    /* R9  */
  *(--sp) = 0U;                    /* R8  */
  *(--sp) = 0U;                    /* R7  */
  *(--sp) = 0U;                    /* R6  */
  *(--sp) = 0U;                    /* R5  */
  *(--sp) = 0U;                    /* R4  */

  return sp;
}

/* ======================================================================
 * Ready-List Management (Bitmap + Circular Linked Lists)
 *
 * Bitmap: bit (31 − prio) is set when prio has ≥1 ready task.
 * __CLZ(bitmap) → highest-priority (lowest-numbered) ready level.
 * ====================================================================== */

void OS_ReadyListInsert(OS_TCB_t *tcb) {
  uint32_t prio = tcb->priority;
  uint32_t bit = 31U - prio;

  if (os_ready_list[prio] == (OS_TCB_t *)0) {
    tcb->next = tcb;
    os_ready_list[prio] = tcb;
  } else {
    tcb->next = os_ready_list[prio]->next;
    os_ready_list[prio]->next = tcb;
  }
  os_ready_bitmap |= (1UL << bit);
}

void OS_ReadyListRemove(OS_TCB_t *tcb) {
  uint32_t prio = tcb->priority;
  uint32_t bit = 31U - prio;

  if (tcb->next == tcb) {
    os_ready_list[prio] = (OS_TCB_t *)0;
    os_ready_bitmap &= ~(1UL << bit);
  } else {
    OS_TCB_t *prev = tcb;
    while (prev->next != tcb) {
      prev = prev->next;
    }
    prev->next = tcb->next;
    if (os_ready_list[prio] == tcb) {
      os_ready_list[prio] = tcb->next;
    }
  }
  tcb->next = (OS_TCB_t *)0;
}

void OS_ReadyListRotate(uint32_t prio) {
  if ((prio < OS_MAX_PRIORITIES) && (os_ready_list[prio] != (OS_TCB_t *)0) &&
      (os_ready_list[prio]->next != os_ready_list[prio])) {
    os_ready_list[prio] = os_ready_list[prio]->next;
  }
}

/* ======================================================================
 * Kernel API
 * ====================================================================== */

void OS_Init(void) {
  uint32_t i;

  os_current_task = (OS_TCB_t *)0;
  os_next_task = (OS_TCB_t *)0;
  os_tick_count = 0U;
  os_ready_bitmap = 0U;
  os_critical_nesting = 0U;
  os_task_count = 0U;

  for (i = 0U; i < OS_MAX_PRIORITIES; i++) {
    os_ready_list[i] = (OS_TCB_t *)0;
  }
  for (i = 0U; i < OS_MAX_TASKS; i++) {
    os_task_table[i] = (OS_TCB_t *)0;
  }

  (void)memset(&os_idle_tcb, 0, sizeof(os_idle_tcb));
  (void)OS_Task_Create(&os_idle_tcb, os_idle_task, (void *)0,
                       OS_MAX_PRIORITIES - 1U, "idle");

  /* Initialise the software timer subsystem (creates daemon task). */
  OS_TimerInit();
}

int32_t OS_Task_Create(OS_TCB_t *tcb, void (*entry)(void *), void *arg,
                       uint32_t priority, const char *name) {
  if ((tcb == (OS_TCB_t *)0) || (entry == (void (*)(void *))0)) {
    return OS_ERR_PARAM;
  }
  if (priority >= OS_MAX_PRIORITIES) {
    return OS_ERR_PARAM;
  }
  if (os_task_count >= OS_MAX_TASKS) {
    return OS_ERR_FULL;
  }

  OS_EnterCritical();

  tcb->priority = priority;
  tcb->state = OS_TASK_READY;
  tcb->delay_ticks = 0U;
  tcb->name = name;
  tcb->next = (OS_TCB_t *)0;
  tcb->blocked_on = (void *)0;
  tcb->block_result = OS_OK;
  tcb->sp = os_stack_init(tcb, entry, arg);

  os_task_table[os_task_count] = tcb;
  os_task_count++;

  OS_ReadyListInsert(tcb);

  OS_ExitCritical();

  if (os_current_task != (OS_TCB_t *)0) {
    OS_Schedule();
  }

  return OS_OK;
}

void OS_Start(void) {
  uint32_t highest_prio;
  uint32_t actual_iclk = CLK_GetActualICLK();  /* Get actual ICLK (200 MHz or 48 MHz fallback) */
  uint32_t systick_reload = (actual_iclk / OS_TICK_RATE_HZ) - 1UL;  /* Calculate reload for actual ICLK */

  /* PendSV + SysTick → lowest NVIC priority (0xFF).
   * Context switches never preempt application ISRs. */
  SCB_SHPR3 = 0xFFFF0000UL;

  /* SysTick: dynamically calculated based on actual ICLK.
   * For 200 MHz: reload = 199999 (1 ms)
   * For 48 MHz:  reload = 47999  (1 ms) */
  SYST_RVR = systick_reload;
  SYST_CVR = 0U;
  SYST_CSR = SYST_CSR_ENABLE | SYST_CSR_TICKINT | SYST_CSR_CLKSOURCE;

  if (os_ready_bitmap == 0U) {
    for (;;) {
      __asm volatile("nop");
    }
  }

  highest_prio = (uint32_t)__builtin_clz(os_ready_bitmap);
  os_current_task = os_ready_list[highest_prio];
  os_next_task = os_current_task;
  os_current_task->state = OS_TASK_RUNNING;

  OS_StartFirstTask(); /* port_cm33.S — never returns */
  for (;;) {
    __asm volatile("nop");
  }
}

void OS_Schedule(void) {
  uint32_t highest_prio;
  OS_TCB_t *candidate;

  if (os_ready_bitmap == 0U) {
    return;
  }

  highest_prio = (uint32_t)__builtin_clz(os_ready_bitmap);
  candidate = os_ready_list[highest_prio];

  if (candidate != os_current_task) {
    if ((os_current_task != (OS_TCB_t *)0) &&
        (os_current_task->state == OS_TASK_RUNNING)) {
      os_current_task->state = OS_TASK_READY;
    }

    candidate->state = OS_TASK_RUNNING;
    os_next_task = candidate;
    OS_TriggerPendSV();
  }
}

void OS_Task_Delay(uint32_t ticks) {
  if (ticks == 0U) {
    return;
  }

  OS_EnterCritical();
  os_current_task->delay_ticks = ticks;
  os_current_task->state = OS_TASK_BLOCKED;
  os_current_task->blocked_on = (void *)0;
  OS_ReadyListRemove(os_current_task);
  OS_ExitCritical();

  OS_Schedule();
}

void OS_Yield(void) {
  OS_EnterCritical();
  OS_ReadyListRotate(os_current_task->priority);
  OS_ExitCritical();
  OS_Schedule();
}

uint32_t OS_GetTick(void) { return os_tick_count; }

void OS_EnterCritical(void) {
  __asm volatile("cpsid i" ::: "memory");
  os_critical_nesting++;
}

void OS_ExitCritical(void) {
  os_critical_nesting--;
  if (os_critical_nesting == 0U) {
    __asm volatile("cpsie i" ::: "memory");
  }
}

void OS_TriggerPendSV(void) {
  SCB_ICSR = ICSR_PENDSVSET;
  __asm volatile("dsb" ::: "memory");
  __asm volatile("isb" ::: "memory");
}

/* ======================================================================
 * SysTick_Handler — 1 ms Kernel Heartbeat
 *
 * 1. Increment tick counter.
 * 2. Scan blocked tasks: handle delay expiry & semaphore timeouts.
 * 3. Tick software timers.
 * 4. Round-robin rotation at current priority level.
 * 5. Reschedule.
 * ====================================================================== */

void SysTick_Handler(void) {
  uint32_t i;
  OS_TCB_t *tcb;

  OS_EnterCritical();

  os_tick_count++;

  /* --- Process blocked tasks --- */
  for (i = 0U; i < os_task_count; i++) {
    tcb = os_task_table[i];
    if ((tcb == (OS_TCB_t *)0) || (tcb->state != OS_TASK_BLOCKED)) {
      continue;
    }

    /* Skip tasks waiting forever (semaphore with no timeout). */
    if (tcb->delay_ticks == OS_WAIT_FOREVER) {
      continue;
    }

    if (tcb->delay_ticks > 0U) {
      tcb->delay_ticks--;
    }

    if (tcb->delay_ticks == 0U) {
      /* Check if blocked on a semaphore (timeout case). */
      if (tcb->blocked_on != (void *)0) {
        OS_Sem_TimeoutHandler((Semaphore_t *)tcb->blocked_on, tcb);
        tcb->block_result = OS_ERR_TIMEOUT;
        tcb->blocked_on = (void *)0;
      }

      tcb->state = OS_TASK_READY;
      OS_ReadyListInsert(tcb);
    }
  }

  /* --- Tick software timers --- */
  OS_TimerTick();

  /* --- Round-Robin rotation at current priority --- */
  if (os_current_task != (OS_TCB_t *)0) {
    OS_ReadyListRotate(os_current_task->priority);
  }

  OS_ExitCritical();

  OS_Schedule();
}

/* ======================================================================
 * OS_StackOverflowCheck — canary-based per-task stack overflow detection.
 *
 * Iterates all registered tasks and verifies that the canary word planted
 * at stack[0] (lowest address, first overwritten on overflow) still holds
 * the value OS_STACK_CANARY_VALUE.
 *
 * Returns: 0 = all intact; (i + 1) = 1-based index of first bad task.
 * ====================================================================== */
uint32_t OS_StackOverflowCheck(void)
{
    uint32_t i;
    for (i = 0U; i < os_task_count; i++)
    {
        if ((os_task_table[i] != (OS_TCB_t *)0) &&
            (os_task_table[i]->stack[0] != OS_STACK_CANARY_VALUE))
        {
            return i + 1U;   /* 1-based index of corrupted task */
        }
    }
    return 0U;   /* all canaries intact */
}
