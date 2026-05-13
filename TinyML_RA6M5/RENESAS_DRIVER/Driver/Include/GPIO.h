#ifndef GPIO_H
#define GPIO_H
#include <stdint.h>

/*
 * GPIO.h — GPIO driver for RA6M5.
 *
 * Port index mapping:
 *   GPIO_PORT0..9 → index 0..9
 *   GPIO_PORTA    → index 10
 *   GPIO_PORTB    → index 11
 *   Invalid port  → GPIO_PortToIndex returns 0xFF (sentinel)
 *
 * GPIO_MODE_t: RA6M5 has no drive-speed register (unlike STM32).
 *   Use GPIO_MODE_INPUT or GPIO_MODE_OUTPUT only.
 */

/* -----------------------------------------------------------------------
 * Port control registers (PCNTR)
 * RA6M5 HW manual §19.2: PORT_BASE = 0x40080000, stride = 0x20 per port
 * ----------------------------------------------------------------------- */
#define PORT_BASE       0x40080000UL
#define PORT_STRIDE     0x0020UL
#define PORT_ADDR(p)    (PORT_BASE + PORT_STRIDE * (uint32_t)(p))

#define PORT_PCNTR1(p)  (*(volatile uint32_t *)(uintptr_t)(PORT_ADDR(p) + 0x000U))
#define PORT_PDR(p)     (*(volatile uint16_t *)(uintptr_t)(PORT_ADDR(p) + 0x002U))  /* direction */
#define PORT_PCNTR2(p)  (*(volatile uint32_t *)(uintptr_t)(PORT_ADDR(p) + 0x004U))  /* PIDR/EODR */
#define PORT_PCNTR3(p)  (*(volatile uint32_t *)(uintptr_t)(PORT_ADDR(p) + 0x008U))  /* POSR/PORR */
#define PORT_PCNTR4(p)  (*(volatile uint32_t *)(uintptr_t)(PORT_ADDR(p) + 0x00CU))

/* -----------------------------------------------------------------------
 * Pin Function Select (PFS) registers
 * PFS_BASE = 0x40080800, PmnPFS(m,n) = base + 0x40*m + 0x04*n
 * PWPR: write-protect register for PFS (must unlock before writing PFS)
 * ----------------------------------------------------------------------- */
#define PFS_BASE         0x40080800UL

#define PmnPFS_PMR       (1U << 16)        /* bit16: 0=GPIO, 1=Peripheral function  */
#define PmnPFS_NCODR     (1U << 6)         /* bit6:  N-channel open-drain           */
#define PmnPFS_PCR       (1U << 4)         /* bit4:  Pull-up resistor control       */
#define PmnPFS_PSEL(x)   ((uint32_t)(x) << 24U) /* bits[28:24]: peripheral select   */

#define PmnPFS(m, n)     (*(volatile uint32_t *)(uintptr_t)(PFS_BASE + 0x040U*(m) + 0x004U*(n)))
#define PWPR             (*(volatile uint8_t  *)(uintptr_t)(PFS_BASE + 0x503U))

/* -----------------------------------------------------------------------
 * Port enum — values are ASCII characters used by GPIO_PortToIndex()
 * ----------------------------------------------------------------------- */
typedef enum {
    GPIO_PORT0 = '0',
    GPIO_PORT1 = '1',
    GPIO_PORT2 = '2',
    GPIO_PORT3 = '3',
    GPIO_PORT4 = '4',
    GPIO_PORT5 = '5',
    GPIO_PORT6 = '6',
    GPIO_PORT7 = '7',
    GPIO_PORT8 = '8',
    GPIO_PORT9 = '9',
    GPIO_PORTA = 'A',
    GPIO_PORTB = 'B'
} GPIO_PORT_t;

/* -----------------------------------------------------------------------
 * Pin configuration enum (CNF)
 * ----------------------------------------------------------------------- */
typedef enum {
    GPIO_CNF_IN_FLT  = 0,   /* digital input, floating (no pull)               */
    GPIO_CNF_IN_PU   = 1,   /* digital input with internal pull-up             */
    GPIO_CNF_IN_PD   = 2,   /* pull-down (RA6M5 has none; use external resistor)*/
    GPIO_CNF_IN_ANA  = 3,   /* analog mode (ASEL=1)                            */
    GPIO_CNF_IN_RES  = 4,   /* reserved                                        */
    GPIO_CNF_OUT_PP  = 5,   /* push-pull output                                */
    GPIO_CNF_OUT_OD  = 6,   /* open-drain output (NCODR=1)                     */
    GPIO_CNF_AF_PP   = 7,   /* peripheral function, push-pull  (PMR=1)         */
    GPIO_CNF_AF_OD   = 8    /* peripheral function, open-drain (PMR=1, NCODR=1)*/
} GPIO_CNF_t;

/* -----------------------------------------------------------------------
 * Direction enum (MODE)
 * RA6M5 has no drive-speed register — INPUT/OUTPUT only.
 * ----------------------------------------------------------------------- */
typedef enum {
    GPIO_MODE_INPUT  = 0,
    GPIO_MODE_OUTPUT = 1
} GPIO_MODE_t;

/* -----------------------------------------------------------------------
 * Pin state enum
 * ----------------------------------------------------------------------- */
typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET   = 1
} GPIO_PINSTATE_t;

/* -----------------------------------------------------------------------
 * Invalid port sentinel — returned by GPIO_PortToIndex on bad input.
 * Callers must check for this value before using the index.
 * ----------------------------------------------------------------------- */
#define GPIO_INVALID_PORT  0xFFU

/* -----------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */
uint8_t GPIO_PortToIndex(GPIO_PORT_t port);
void    GPIO_Config(GPIO_PORT_t port, uint8_t pin, GPIO_CNF_t cnf, GPIO_MODE_t mode);
void    GPIO_Write_Pin(GPIO_PORT_t port, uint8_t pin, GPIO_PINSTATE_t state);
uint8_t GPIO_Read_Pin(GPIO_PORT_t port, uint8_t pin);

#endif /* GPIO_H */
