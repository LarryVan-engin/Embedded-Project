/**
 * @file    rtos_config.h
 * @brief   Central RTOS Configuration — Renesas RA6M5 (Cortex-M33)
 *
 * All tunable kernel parameters in one place (cf. FreeRTOSConfig.h).
 * Every OS module includes this header for sizing and timing constants.
 */

#ifndef RTOS_CONFIG_H
#define RTOS_CONFIG_H

#include "board_config.h"

/* ======================================================================
 * Clock & Tick
 * ====================================================================== */

/** ICLK frequency in Hz.  CLK_Init() selects PLL /1 → 200 MHz. */
#define OS_CPU_CLOCK_HZ             200000000UL

/** Kernel tick rate.  1000 Hz → 1 ms per tick. */
#define OS_TICK_RATE_HZ             1000U

/** SysTick reload value.  (CPU_CLK / TICK_RATE) − 1 = 199 999. */
#define OS_SYSTICK_RELOAD           ((OS_CPU_CLOCK_HZ / OS_TICK_RATE_HZ) - 1UL)

/* ======================================================================
 * Task Limits
 * ====================================================================== */

/** Maximum concurrent tasks (including idle + timer daemon). */
#define OS_MAX_TASKS                32U

/** Number of discrete priority levels.  0 = highest, N−1 = lowest. */
#define OS_MAX_PRIORITIES           32U

/** Default per-task stack in bytes.
 *  16 KB: TFLite Micro Invoke() can spike to ~2 KB on the IAQ task
 *  (FP context + operator call chain + debug_print vsprintf buffer).
 *  The original 8 KB was observed to produce complete MCU halts after
 *  ~18 minutes via HardFault from stack overflow.  16 KB provides ~14 KB
 *  of working headroom above the OS_STACK_CANARY_VALUE sentinel.
 *  RA6M5 has 512 KB SRAM; with up to 12 active tasks the total stack
 *  footprint is 12 × 16 KB = 192 KB, well within budget.
 */
#define OS_DEFAULT_STACK_SIZE       16384U

/** Stack size in 32-bit words (derived). */
#define OS_DEFAULT_STACK_WORDS      (OS_DEFAULT_STACK_SIZE / 4U)

/* ======================================================================
 * Semaphore Limits
 * ====================================================================== */

/** Max tasks that can pend on a single semaphore simultaneously. */
#define OS_SEM_MAX_WAITERS          16U

/* ======================================================================
 * Software Timer Configuration
 * ====================================================================== */

/** Maximum number of software timers that can be registered. */
#define OS_MAX_SOFTWARE_TIMERS      16U

/** Timer daemon task priority.  High (1) so callbacks run promptly. */
#define OS_TIMER_TASK_PRIORITY      1U

/** Timer daemon stack size in bytes. */
#define OS_TIMER_TASK_STACK_SIZE    4096U

/* ======================================================================
 * API Return Codes
 * ====================================================================== */

#define OS_OK                       ( 0)    /**< Operation succeeded.      */
#define OS_ERR_TIMEOUT              (-1)    /**< Timed out waiting.        */
#define OS_ERR_PARAM                (-2)    /**< Invalid parameter.        */
#define OS_ERR_FULL                 (-3)    /**< Resource table full.      */
#define OS_ERR_ISR                  (-4)    /**< Illegal call from ISR.    */

/* ======================================================================
 * Timeout Sentinels
 * ====================================================================== */

/** Block indefinitely until resource is available. */
#define OS_WAIT_FOREVER             0xFFFFFFFFU

/** Do not block — return immediately if resource unavailable. */
#define OS_NO_WAIT                  0U

/* ======================================================================
 * Debug Output Configuration
 * ====================================================================== */

/**
 * Master switch for debug_print().
 * Set to 0 to eliminate all debug code from the binary (zero overhead).
 */
#define OS_DEBUG_ENABLE             1

/**
 * Backend selection — set exactly one to 1.
 *
 *   OS_DEBUG_BACKEND_UART     : bare-metal SCI UART (default, no OS required)
 *   OS_DEBUG_BACKEND_SEMIHOST : ARM semihosting via JTAG/SWD debugger
 *                               (requires --specs=rdimon.specs at link time)
 */
#define OS_DEBUG_BACKEND_UART       1
#define OS_DEBUG_BACKEND_SEMIHOST   0
#define OS_DEBUG_BACKEND_USB_CDC    0

/**
 * SCI channel used for UART output (0–9).
 * Automatically set from board_config.h based on BOARD_TYPE.
 */
#define OS_DEBUG_UART_CHANNEL       DEBUG_UART_CHANNEL

/** Baud rate for the debug UART channel (from board_config.h). */
#define OS_DEBUG_UART_BAUDRATE      DEBUG_UART_BAUDRATE

/**
 * UART BRR divisor mode for bring-up tuning (from board_config.h).
 * 4U: SEMR BGDM=1 + ABCS=1  (effective /4)
 * 8U: SEMR BGDM=1 + ABCS=0  (effective /8)
 */
#define OS_DEBUG_UART_BRR_DIVISOR   DEBUG_UART_BRR_DIV

#endif /* RTOS_CONFIG_H */
