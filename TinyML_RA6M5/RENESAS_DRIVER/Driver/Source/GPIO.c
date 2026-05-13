#include "GPIO.h"

/* -----------------------------------------------------------------------
 * GPIO_PortToIndex — convert GPIO_PORT_t enum to hardware port index.
 *
 * Returns 0..9 for PORT0..9, 10 for PORTA, 11 for PORTB.
 * Returns GPIO_INVALID_PORT (0xFF) for any unrecognised value.
 * Callers MUST check the return value before using it as a register index.
 * ----------------------------------------------------------------------- */
uint8_t GPIO_PortToIndex(GPIO_PORT_t port)
{
    if (port >= '0' && port <= '9')
        return (uint8_t)(port - '0');
    if (port >= 'A' && port <= 'B')
        return (uint8_t)(port - 'A' + 10U);
    return GPIO_INVALID_PORT;   /* 0xFF sentinel — invalid port */
}

/* -----------------------------------------------------------------------
 * GPIO_Config — configure a pin's function and direction.
 *
 * PWPR unlock sequence (RA6M5 HW manual §19.2.5):
 *   1. PWPR = 0x00  clear B0WI  (allows changing PFSWE)
 *   2. PWPR = 0x40  set  PFSWE  (allows writing PFS registers)
 *      ... write PFS ...
 *   3. PWPR = 0x00  clear PFSWE
 *   4. PWPR = 0x80  set  B0WI   (locks PFS writes)
 *
 * RA6M5 has no built-in pull-down; GPIO_CNF_IN_PD needs an external resistor.
 * ----------------------------------------------------------------------- */
void GPIO_Config(GPIO_PORT_t port, uint8_t pin, GPIO_CNF_t cnf, GPIO_MODE_t mode)
{
    uint8_t p = GPIO_PortToIndex(port);
    if (p == GPIO_INVALID_PORT) { return; }

    PWPR = 0x00U;   /* Step 1: clear B0WI  */
    PWPR = 0x40U;   /* Step 2: set  PFSWE  */

    PmnPFS(p, pin) = 0x00000000U;   /* reset: GPIO, no analog, no IRQ, no pull */

    switch (cnf)
    {
        case GPIO_CNF_IN_PU:
            PmnPFS(p, pin) |= PmnPFS_PCR;                    /* pull-up              */
            break;
        case GPIO_CNF_OUT_OD:
            PmnPFS(p, pin) |= PmnPFS_NCODR;                  /* N-ch open-drain      */
            break;
        case GPIO_CNF_AF_PP:
            PmnPFS(p, pin) |= PmnPFS_PMR;                    /* peripheral function  */
            break;
        case GPIO_CNF_AF_OD:
            PmnPFS(p, pin) |= PmnPFS_PMR | PmnPFS_NCODR;    /* peripheral + OD      */
            break;
        case GPIO_CNF_IN_ANA:
            PmnPFS(p, pin) |= (1U << 15U);                   /* ASEL=1, analog mode  */
            break;
        case GPIO_CNF_IN_FLT:   /* floating input  — PFS already cleared */
        case GPIO_CNF_OUT_PP:   /* push-pull output — PFS already cleared */
        case GPIO_CNF_IN_PD:    /* no HW pull-down on RA6M5              */
        default:
            break;
    }

    PWPR = 0x00U;   /* Step 3: clear PFSWE */
    PWPR = 0x80U;   /* Step 4: set  B0WI   */

    if (mode == GPIO_MODE_INPUT)
        PORT_PDR(p) &= (uint16_t)(~(uint16_t)(1U << pin));   /* PDR=0: input  */
    else
        PORT_PDR(p) |=  (uint16_t)(1U << pin);                /* PDR=1: output */
}

/* -----------------------------------------------------------------------
 * GPIO_Write_Pin — atomically drive a GPIO output pin HIGH or LOW.
 *
 * PCNTR3 layout (RA6M5 HW manual §19.2.2):
 *   bits [15: 0] = POSR  — write 1 to drive pin HIGH
 *   bits [31:16] = PORR  — write 1 to drive pin LOW
 * ----------------------------------------------------------------------- */
void GPIO_Write_Pin(GPIO_PORT_t port, uint8_t pin, GPIO_PINSTATE_t state)
{
    uint8_t p = GPIO_PortToIndex(port);
    if (p == GPIO_INVALID_PORT) { return; }

    if (state == GPIO_PIN_SET)
        PORT_PCNTR3(p) = (uint32_t)(1U << pin);          /* POSR: drive HIGH */
    else
        PORT_PCNTR3(p) = (uint32_t)(1U << pin) << 16U;   /* PORR: drive LOW  */
}

/* -----------------------------------------------------------------------
 * GPIO_Read_Pin — read the current logic level of a GPIO input pin.
 *
 * PCNTR2 bits [15:0] = PIDR (Port Input Data Register).
 * Returns 1 if pin is HIGH, 0 if LOW.
 * ----------------------------------------------------------------------- */
uint8_t GPIO_Read_Pin(GPIO_PORT_t port, uint8_t pin)
{
    uint8_t p = GPIO_PortToIndex(port);
    if (p == GPIO_INVALID_PORT) { return 0U; }

    return (uint8_t)((PORT_PCNTR2(p) >> pin) & 1U);
}
