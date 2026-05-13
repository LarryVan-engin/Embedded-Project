/**
 * @file    software_timer.c
 * @brief   Software Timer Implementation — Daemon Task Architecture
 *
 * Timer callbacks execute in the Timer Daemon Task context (not ISR).
 * SysTick_Handler calls OS_TimerTick() every 1 ms to decrement running
 * timers.  When any timer expires, the daemon is signalled via an
 * internal semaphore.  The daemon then processes all expired timers.
 *
 * This design keeps ISR execution time minimal and deterministic.
 */

#include "software_timer.h"
#include "kernel.h"
#include "semaphore.h"
#include <string.h>

/* ======================================================================
 * Internal State
 * ====================================================================== */

/** Registry of all created timers. */
static Timer_t  *os_timer_list[OS_MAX_SOFTWARE_TIMERS];
static uint32_t  os_timer_count;

/** Semaphore used to wake the timer daemon when timers expire. */
static Semaphore_t os_timer_sem;

/** Timer daemon task TCB and custom stack. */
static OS_TCB_t os_timer_daemon_tcb;

/* ======================================================================
 * Timer Daemon Task
 *
 * Runs at OS_TIMER_TASK_PRIORITY (high priority = 1).
 * Blocks on os_timer_sem.  When signalled, iterates through all timers
 * and calls callbacks for expired ones.
 * ====================================================================== */

static void os_timer_daemon(void *arg)
{
    (void)arg;
    uint32_t i;

    for (;;) {
        /* Block until SysTick signals that ≥1 timer has expired. */
        (void)OS_SemPend(&os_timer_sem, OS_WAIT_FOREVER);

        /* Process all expired timers. */
        OS_EnterCritical();

        for (i = 0U; i < os_timer_count; i++) {
            Timer_t *t = os_timer_list[i];
            if ((t != (Timer_t *)0) && (t->state == OS_TIMER_EXPIRED)) {
                if (t->mode == OS_TIMER_AUTO_RELOAD) {
                    /* Restart: reload period and keep running. */
                    t->remaining = t->period;
                    t->state     = OS_TIMER_RUNNING;
                } else {
                    /* One-shot: stop after firing. */
                    t->state = OS_TIMER_STOPPED;
                }

                /* Release critical section for callback execution.
                 * Callbacks may call OS APIs (delay, sem, etc.). */
                OS_ExitCritical();

                if (t->callback != (TimerCallback_t)0) {
                    t->callback(t->arg);
                }

                OS_EnterCritical();
            }
        }

        OS_ExitCritical();
    }
}

/* ======================================================================
 * API Implementation
 * ====================================================================== */

void OS_TimerInit(void)
{
    os_timer_count = 0U;
    (void)memset(os_timer_list, 0, sizeof(os_timer_list));

    /* Create the internal semaphore (binary, starts at 0 = empty). */
    (void)OS_SemCreate(&os_timer_sem, 0U, 1U);

    /* Create the timer daemon task. */
    (void)memset(&os_timer_daemon_tcb, 0, sizeof(os_timer_daemon_tcb));
    (void)OS_Task_Create(&os_timer_daemon_tcb,
                         os_timer_daemon,
                         (void *)0,
                         OS_TIMER_TASK_PRIORITY,
                         "tmr_daemon");
}

int32_t OS_TimerCreate(Timer_t *timer, TimerCallback_t cb, void *arg,
                       uint32_t period_ticks, TimerMode_t mode)
{
    if ((timer == (Timer_t *)0) || (cb == (TimerCallback_t)0) || (period_ticks == 0U)) {
        return OS_ERR_PARAM;
    }

    OS_EnterCritical();

    if (os_timer_count >= OS_MAX_SOFTWARE_TIMERS) {
        OS_ExitCritical();
        return OS_ERR_FULL;
    }

    timer->callback  = cb;
    timer->arg       = arg;
    timer->period    = period_ticks;
    timer->remaining = period_ticks;
    timer->mode      = mode;
    timer->state     = OS_TIMER_STOPPED;

    os_timer_list[os_timer_count] = timer;
    os_timer_count++;

    OS_ExitCritical();
    return OS_OK;
}

int32_t OS_TimerStart(Timer_t *timer)
{
    if (timer == (Timer_t *)0) { return OS_ERR_PARAM; }

    OS_EnterCritical();
    timer->remaining = timer->period;
    timer->state     = OS_TIMER_RUNNING;
    OS_ExitCritical();

    return OS_OK;
}

int32_t OS_TimerStop(Timer_t *timer)
{
    if (timer == (Timer_t *)0) { return OS_ERR_PARAM; }

    OS_EnterCritical();
    timer->state = OS_TIMER_STOPPED;
    OS_ExitCritical();

    return OS_OK;
}

int32_t OS_TimerReset(Timer_t *timer)
{
    if (timer == (Timer_t *)0) { return OS_ERR_PARAM; }

    OS_EnterCritical();
    timer->remaining = timer->period;
    if (timer->state != OS_TIMER_RUNNING) {
        timer->state = OS_TIMER_RUNNING;
    }
    OS_ExitCritical();

    return OS_OK;
}

/**
 * @brief  Called from SysTick_Handler every 1 ms.
 *
 * Decrements all running timers.  If any expire, marks them EXPIRED
 * and posts the daemon semaphore (once, even if multiple expire).
 * The daemon then processes all expired timers in task context.
 */
void OS_TimerTick(void)
{
    uint32_t i;
    uint32_t any_expired = 0U;

    for (i = 0U; i < os_timer_count; i++) {
        Timer_t *t = os_timer_list[i];
        if ((t != (Timer_t *)0) && (t->state == OS_TIMER_RUNNING)) {
            if (t->remaining > 0U) {
                t->remaining--;
            }
            if (t->remaining == 0U) {
                t->state    = OS_TIMER_EXPIRED;
                any_expired = 1U;
            }
        }
    }

    /* Signal the daemon (once) if any timer expired. */
    if (any_expired != 0U) {
        (void)OS_SemPost(&os_timer_sem);
    }
}
