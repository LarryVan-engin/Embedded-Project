/**
 * @file    semaphore.h
 * @brief   Counting & Binary Semaphore API
 *
 * Provides blocking synchronisation with optional timeout.
 * A Binary semaphore is simply a Counting semaphore with max_count = 1.
 * Wait list is priority-ordered: highest-priority waiter wakes first.
 */

#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <stdint.h>
#include "kernel.h"

/* ======================================================================
 * Semaphore Structure
 * ====================================================================== */

typedef struct {
    volatile uint32_t  count;                           /**< Current count.    */
    uint32_t           max_count;                       /**< Upper bound.      */
    OS_TCB_t          *wait_list[OS_SEM_MAX_WAITERS];   /**< Blocked tasks.    */
    uint32_t           wait_count;                      /**< # tasks waiting.  */
} Semaphore_t;

/* ======================================================================
 * API
 * ====================================================================== */

/**
 * @brief  Initialise a semaphore.
 * @param  sem           Pointer to caller-allocated Semaphore_t.
 * @param  initial_count Starting count (0 for "taken", N for available).
 * @param  max_count     Maximum count (1 = binary semaphore).
 * @return OS_OK on success, OS_ERR_PARAM on bad input.
 */
int32_t OS_SemCreate(Semaphore_t *sem, uint32_t initial_count, uint32_t max_count);

/**
 * @brief  Acquire (decrement) the semaphore.  Blocks if count == 0.
 * @param  sem     Pointer to semaphore.
 * @param  timeout Ticks to wait (OS_NO_WAIT, OS_WAIT_FOREVER, or N ms).
 * @return OS_OK on success, OS_ERR_TIMEOUT on timeout, OS_ERR_PARAM on bad input.
 */
int32_t OS_SemPend(Semaphore_t *sem, uint32_t timeout);

/**
 * @brief  Release (increment) the semaphore.  Wakes highest-priority waiter.
 *         Safe to call from ISR context (SysTick, etc.).
 * @param  sem  Pointer to semaphore.
 * @return OS_OK on success, OS_ERR_PARAM on bad input.
 */
int32_t OS_SemPost(Semaphore_t *sem);

/**
 * @brief  Internal: remove a task from a semaphore's wait list.
 *         Called by kernel SysTick when a semaphore wait times out.
 */
void OS_Sem_TimeoutHandler(Semaphore_t *sem, OS_TCB_t *tcb);

#endif /* SEMAPHORE_H */
