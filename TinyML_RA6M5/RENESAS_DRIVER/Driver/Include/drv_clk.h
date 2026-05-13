#ifndef DRV_CLK_H
#define DRV_CLK_H
#include <stdint.h>
#include "drv_common.h"

/*
 * drv_clk.h — Clock and Module Stop control for RA6M5.
 *
 * Replaces: CGC.h (clock registers) + LPM.h (MSTPCR registers).
 * S-01 fix: LPM.h was misnamed — it contained MSTPCR, not LPM logic.
 *
 * Default clock after reset: MOCO 8 MHz, all dividers = /1.
 * CLK_Init() switches to the configured production clock tree.
 */

/* -----------------------------------------------------------------------
 * Clock Generation Circuit (CGC) registers  (RA6M5 HW Manual §8)
 * Base: SYSC = 0x4001E000
 * ----------------------------------------------------------------------- */
#define SCKDIVCR    (*(volatile uint32_t *)(uintptr_t)(SYSC + 0x020U))  /* System Clock Division Control   */
#define SCKSCR      (*(volatile uint8_t  *)(uintptr_t)(SYSC + 0x026U))  /* System Clock Source Control     */
#define PLLCCR      (*(volatile uint16_t *)(uintptr_t)(SYSC + 0x028U))  /* PLL Clock Control               */
#define PLLCR       (*(volatile uint8_t  *)(uintptr_t)(SYSC + 0x02AU))  /* PLL Control                     */
#define PLL2CCR     (*(volatile uint16_t *)(uintptr_t)(SYSC + 0x048U))  /* PLL2 Clock Control              */
#define PLL2CR      (*(volatile uint8_t  *)(uintptr_t)(SYSC + 0x04AU))  /* PLL2 Control                    */
#define MOSCCR      (*(volatile uint8_t  *)(uintptr_t)(SYSC + 0x032U))  /* Main Clock Oscillator Control   */
#define HOCOCR      (*(volatile uint8_t  *)(uintptr_t)(SYSC + 0x036U))  /* HOCO Control                    */
#define MOCOCR      (*(volatile uint8_t  *)(uintptr_t)(SYSC + 0x038U))  /* MOCO Control                    */
#define OSCSF       (*(volatile uint8_t  *)(uintptr_t)(SYSC + 0x03CU))  /* Oscillation Stabilization Flag  */
#define MOSCWTCR    (*(volatile uint8_t  *)(uintptr_t)(SYSC + 0x0A2U))  /* Main Osc Wait Control           */
#define MOMCR       (*(volatile uint8_t  *)(uintptr_t)(SYSC + 0x413U))  /* Main Osc Mode Control           */
#define USBCKDIVCR  (*(volatile uint8_t  *)(uintptr_t)(SYSC + 0x06CU))  /* USB Clock Division Control      */
#define USBCKCR     (*(volatile uint8_t  *)(uintptr_t)(SYSC + 0x074U))  /* USB Clock Control               */

#define FCACHE_BASE 0x4001C000UL
#define SRAM_BASE   0x40002000UL

#define FLWT        (*(volatile uint8_t  *)(uintptr_t)(FCACHE_BASE + 0x11CU)) /* Flash Wait Cycle Register */
#define SRAMPRCR    (*(volatile uint8_t  *)(uintptr_t)(SRAM_BASE + 0x004U))   /* SRAM Protection Register  */
#define SRAMWTSC    (*(volatile uint8_t  *)(uintptr_t)(SRAM_BASE + 0x008U))   /* SRAM Wait State Control   */

/* SCKSCR.CKSEL[2:0] clock source values */
#define SCKSCR_HOCO  0x00U   /* HOCO (up to 64 MHz) */
#define SCKSCR_MOCO  0x01U   /* MOCO (8 MHz, reset default) */
#define SCKSCR_LOCO  0x02U   /* LOCO (32 kHz) */
#define SCKSCR_MOSC  0x03U   /* Main clock oscillator */
#define SCKSCR_PLL   0x05U   /* PLL */

/* SCKDIVCR divider encoding (same for all fields) */
#define SCKDIV_1     0x0U
#define SCKDIV_2     0x1U
#define SCKDIV_4     0x2U
#define SCKDIV_8     0x3U
#define SCKDIV_16    0x4U
#define SCKDIV_32    0x5U
#define SCKDIV_64    0x6U

/* SCKDIVCR field bit positions */
#define SCKDIVCR_PCKDPOS   0U    /* PCLKD [2:0] */
#define SCKDIVCR_PCKCPOS   4U    /* PCLKC [6:4] */
#define SCKDIVCR_PCKBPOS   8U    /* PCLKB [10:8] */
#define SCKDIVCR_PCKAPOS  12U    /* PCLKA [14:12] */
#define SCKDIVCR_BCKPOS   16U    /* BCLK  [18:16] */
#define SCKDIVCR_ICKPOS   24U    /* ICLK  [26:24] */
#define SCKDIVCR_FCKPOS   28U    /* FCLK  [30:28] */

#define PLLCR_PLLSTP      (1U << 0)
#define PLL2CR_PLL2STP    (1U << 0)

#define MOSCCR_MOSTP      (1U << 0)

#define OSCSF_HOCOSF      (1U << 0)
#define OSCSF_MOSCSF      (1U << 3)
#define OSCSF_PLLSF       (1U << 5)
#define OSCSF_PLL2SF      (1U << 6)

#define MOSCWTCR_MSTS_9   0x09U

#define FLWT_3_WAIT       0x03U

#define SRAMPRCR_KEY_UNLOCK  0xF1U
#define SRAMPRCR_KEY_LOCK    0xF0U
#define SRAMWTSC_WAIT_1      0x01U

#define PLLCCR_PLLMUL_POS    8U
#define PLLCCR_PLSRCSEL_POS  4U

#define PLL_SOURCE_MOSC      0U
#define PLL_DIV_3            0x02U
#define PLL_MUL_X25          49U

#define PLL2_SOURCE_MOSC     0U
#define PLL2_DIV_2           0x01U
#define PLL2_MUL_X20         39U

#define USBCKDIV_5           0x06U
#define USBCKSEL_PLL2        0x06U
#define USBCKCR_USBCKSEL_MASK 0x07U
#define USBCKCR_USBCKSREQ    (1U << 6)
#define USBCKCR_USBCKSRDY    (1U << 7)

/* -----------------------------------------------------------------------
 * Module Stop Control registers  (RA6M5 HW Manual §10.2.4–10.2.7)
 *
 * MSTP block base: 0x4008_4000  (separate from SYSC — verified p.220)
 *   MSTPCRA offset 0x000  MSTPCRB offset 0x004
 *   MSTPCRC offset 0x008  MSTPCRD offset 0x00C
 *
 * bit=1: module clock stopped (reset default)
 * bit=0: module clock running  ← clear bit to enable
 *
 * NOTE: prior code used SYSC+0x700 = 0x4001E700 which is WRONG.
 *       That address is outside the MSTP block and writes were silently
 *       discarded → SCI7 never released from module stop → TDRE stuck at 0.
 * ----------------------------------------------------------------------- */
#define MSTP_BASE   0x40084000UL
#define MSTPCRA     (*(volatile uint32_t *)(uintptr_t)(MSTP_BASE + 0x000U))
#define MSTPCRB     (*(volatile uint32_t *)(uintptr_t)(MSTP_BASE + 0x004U))
#define MSTPCRC     (*(volatile uint32_t *)(uintptr_t)(MSTP_BASE + 0x008U))
#define MSTPCRD     (*(volatile uint32_t *)(uintptr_t)(MSTP_BASE + 0x00CU))

/* -----------------------------------------------------------------------
 * SCI channel enumeration
 * Used by CLK_ModuleStart_SCI().
 * MSTPCRB bit mapping: SCI0=bit31, SCI1=bit30, ..., SCI9=bit22
 * ----------------------------------------------------------------------- */
typedef enum {
    SCI0 = 0, SCI1 = 1, SCI2 = 2, SCI3 = 3, SCI4 = 4,
    SCI5 = 5, SCI6 = 6, SCI7 = 7, SCI8 = 8, SCI9 = 9
} SCI_t;

/* -----------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */

/*
 * CLK_Init — production clock configuration.
 * Call once from Reset_Handler before main().
 * Configures XTAL 24 MHz -> PLL 200 MHz, with RA6M5-required
 * flash and SRAM wait states, then applies the target dividers.
 * Requires RWP unlock (handled internally).
 */
void CLK_Init(void);

/**
 * @brief Return 1 if system fell back to HOCO due to PLL/MOSC timeout, 0 if on production clock.
 * Useful for debug diagnostics to understand actual system speed.
 */
uint8_t CLK_GetFallbackOccurred(void);

/**
 * @brief Return the actual ICLK frequency (200 MHz production, 48 MHz fallback).
 * Used by kernel SysTick initialization to calculate correct reload value.
 */
uint32_t CLK_GetActualICLK(void);

/**
 * @brief Return the actual SCI/UART clock used for BRR calculation.
 * In this project SCI is configured from the PCLKB domain (50 MHz production, 48 MHz fallback).
 */
uint32_t CLK_GetActualSCIClock(void);

/*
 * CLK_ModuleStart_SCI — release module stop for one SCI channel.
 * Replaces LPM_Unlock(). Wraps MSTPCRB write with PRCR unlock/lock.
 */
void CLK_ModuleStart_SCI(SCI_t peripheral);

#endif /* DRV_CLK_H */
