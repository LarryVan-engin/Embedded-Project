/**
 * @file    kernel.h
 * @brief   Preemptive RTOS Kernel — Public API
 *
 * Priority-based preemption with Round-Robin at equal priority levels.
 * O(1) scheduling via 32-bit priority bitmap and __CLZ.
 * Static memory only — zero malloc/new.
 *
 * CRITICAL: 'sp' MUST remain at offset 0 in OS_TCB_t.
 * The PendSV handler (port_cm33.S) depends on this layout.
 */

#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include "rtos_config.h"

/* ======================================================================
 * Task States
 * ====================================================================== */

typedef enum {
    OS_TASK_READY     = 0U,
    OS_TASK_RUNNING   = 1U,
    OS_TASK_BLOCKED   = 2U,
    OS_TASK_SUSPENDED = 3U
} OS_TaskState_t;

/* ======================================================================
 * Task Control Block (TCB)
 * ====================================================================== */

typedef struct OS_TCB {
    uint32_t          *sp;                            /**< Saved SP (OFFSET 0).    */
    struct OS_TCB     *next;                          /**< Round-robin ring link.  */
    uint32_t           priority;                      /**< 0=highest, 31=lowest.   */
    OS_TaskState_t     state;                         /**< Current task state.     */
    uint32_t           delay_ticks;                   /**< Remaining delay/timeout.*/
    const char        *name;                          /**< Debug name.             */
    void              *blocked_on;                    /**< Semaphore ptr or NULL.  */
    int32_t            block_result;                  /**< OS_OK or OS_ERR_TIMEOUT.*/
    uint32_t           stack[OS_DEFAULT_STACK_WORDS]; /**< Static stack (4 KB).    */
} OS_TCB_t;

/* ======================================================================
 * Kernel Globals (used by port_cm33.S)
 * ====================================================================== */

extern OS_TCB_t *volatile os_current_task;
extern OS_TCB_t *volatile os_next_task;

/* ======================================================================
 * Kernel API
 * ====================================================================== */

void     OS_Init(void);
int32_t  OS_Task_Create(OS_TCB_t *tcb, void (*entry)(void *), void *arg,
                        uint32_t priority, const char *name);
void     OS_Start(void);
void     OS_Task_Delay(uint32_t ticks);
void     OS_Yield(void);
void     OS_Schedule(void);
uint32_t OS_GetTick(void);
void     OS_EnterCritical(void);
void     OS_ExitCritical(void);
void     OS_TriggerPendSV(void);

/* Port-layer (port_cm33.S) */
extern void OS_StartFirstTask(void);

/* ======================================================================
 * Stack Overflow Detection
 *
 * OS_Task_Create() writes OS_STACK_CANARY_VALUE to the lowest address of
 * each task's stack array (tcb->stack[0]).  The stack grows downward from
 * tcb->stack[OS_DEFAULT_STACK_WORDS], so stack[0] is the first word
 * overwritten on an overflow.
 *
 * OS_StackOverflowCheck() iterates all registered tasks and returns the
 * 1-based index of the first task with a corrupted canary, or 0 if all
 * canaries are intact.
 * ====================================================================== */

/** Magic value planted at the bottom of every task stack. */
#define OS_STACK_CANARY_VALUE  0xDEADC0DEUL

/**
 * @brief Check all task stack canaries for corruption.
 * @return 0 if all stacks are intact;
 *         (task_index + 1) for the first task whose canary is corrupted.
 */
uint32_t OS_StackOverflowCheck(void);

#endif /* KERNEL_H */
