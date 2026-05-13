#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

/*
 * utils.h — Shared application-layer utilities for RA6M5 bare-metal.
 *
 * BUG-13 fix: delay_ms was duplicated in hal_entry.c and main.c.
 * Centralised here as static inline to eliminate the duplicate.
 *
 * Calibration: ~200 MHz PLL, -O0. Each loop iteration ≈ 2 cycles + NOP.
 * 100000 iterations ≈ 1 ms. Re-calibrate if compiler optimisation changes.
 */

static inline void delay_ms(uint32_t ms)
{
    volatile uint32_t count = ms * 100000U;
    while (count-- != 0U)
    {
        __asm volatile ("nop");
    }
}

#endif /* UTILS_H */
