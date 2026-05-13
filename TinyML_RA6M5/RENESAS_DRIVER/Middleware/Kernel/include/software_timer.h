/**
 * @file    software_timer.h
 * @brief   Software Timer API — One-shot & Auto-reload
 *
 * Timers execute callbacks in the context of a dedicated Timer Daemon Task
 * (not in ISR context), keeping SysTick execution time minimal.
 * The daemon runs at OS_TIMER_TASK_PRIORITY (high) so callbacks fire promptly.
 */

#ifndef SOFTWARE_TIMER_H
#define SOFTWARE_TIMER_H

#include <stdint.h>
#include "rtos_config.h"

/* ======================================================================
 * Timer Types
 * ====================================================================== */

typedef void (*TimerCallback_t)(void *arg);

typedef enum {
    OS_TIMER_ONE_SHOT    = 0U,   /**< Fires once, then stops.    */
    OS_TIMER_AUTO_RELOAD = 1U    /**< Fires repeatedly at period.*/
} TimerMode_t;

typedef enum {
    OS_TIMER_STOPPED = 0U,
    OS_TIMER_RUNNING = 1U,
    OS_TIMER_EXPIRED = 2U
} TimerState_t;

/* ======================================================================
 * Timer Structure (statically allocated by caller)
 * ====================================================================== */

typedef struct {
    TimerCallback_t  callback;     /**< Function called on expiry.       */
    void            *arg;          /**< Argument passed to callback.     */
    uint32_t         period;       /**< Period in ticks.                 */
    volatile uint32_t remaining;   /**< Ticks until next expiry.         */
    TimerMode_t      mode;         /**< ONE_SHOT or AUTO_RELOAD.         */
    volatile TimerState_t state;   /**< Current timer state.             */
} Timer_t;

/* ======================================================================
 * API
 * ====================================================================== */

/**
 * @brief  Initialise the timer subsystem.  Creates the timer daemon task.
 *         Called internally by OS_Init() — do not call manually.
 */
void OS_TimerInit(void);

/**
 * @brief  Create and register a software timer (does NOT start it).
 * @return OS_OK, OS_ERR_PARAM, or OS_ERR_FULL.
 */
int32_t OS_TimerCreate(Timer_t *timer, TimerCallback_t cb, void *arg,
                       uint32_t period_ticks, TimerMode_t mode);

/**
 * @brief  Start (or restart) a timer.  Sets remaining = period.
 */
int32_t OS_TimerStart(Timer_t *timer);

/**
 * @brief  Stop a running timer.
 */
int32_t OS_TimerStop(Timer_t *timer);

/**
 * @brief  Reset a timer's countdown to its full period.
 */
int32_t OS_TimerReset(Timer_t *timer);

/**
 * @brief  Internal: called from SysTick_Handler every tick.
 *         Decrements running timers and signals the daemon on expiry.
 */
void OS_TimerTick(void);

#endif /* SOFTWARE_TIMER_H */
