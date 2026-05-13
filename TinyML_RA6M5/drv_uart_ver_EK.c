#include "drv_uart.h"
#include "drv_clk.h"
#include "drv_common.h"
#include "GPIO.h"

/* -----------------------------------------------------------------------
 * uart_clock_init — release module stop for one SCI channel.
 * Internal helper; callers use UART_Init().
 * ----------------------------------------------------------------------- */
static void uart_clock_init(uint8_t n)
{
    CLK_ModuleStart_SCI((SCI_t)n);
}

/* -----------------------------------------------------------------------
 * uart_pin_config — map UART channel to TX/RX port-pin pairs.
 *
 * Pin mapping (EK-RA6M5 schematic + RA6M5 pin function table):
 *   UART0 : RX=P110, TX=P111  PSEL=0x04
 *   UART1 : RX=P708, TX=P709  PSEL=0x05
 *   UART2 : RX=P301, TX=P302  PSEL=0x04
 *   UART3 : RX=P309, TX=P310  PSEL=0x05
 *   UART4 : RX=P206, TX=P205  PSEL=0x04
 *   UART5 : RX=P502, TX=P501  PSEL=0x05
 *   UART6 : RX=P505, TX=P506  PSEL=0x04
 *   UART7 : RX=P614, TX=P613  PSEL=0x05  ← board UART
 *   UART8 : RX=P607, TX=PA00  PSEL=0x04
 *   UART9 : RX=P110, TX=P109  PSEL=0x05
 * ----------------------------------------------------------------------- */
static void uart_pin_config(UART_t uart)
{
    uint8_t  tx_port, tx_pin;
    uint8_t  rx_port, rx_pin;
    uint32_t psel;

    switch (uart)
    {
        case UART0: tx_port=1;  tx_pin=1;  rx_port=1;  rx_pin=0;  psel=0x04U; break;
        case UART1: tx_port=7;  tx_pin=9;  rx_port=7;  rx_pin=8;  psel=0x05U; break;
        case UART2: tx_port=3;  tx_pin=2;  rx_port=3;  rx_pin=1;  psel=0x04U; break;
        case UART3: tx_port=3;  tx_pin=10; rx_port=3;  rx_pin=9;  psel=0x05U; break;
        case UART4: tx_port=2;  tx_pin=5;  rx_port=2;  rx_pin=6;  psel=0x04U; break;
        case UART5: tx_port=5;  tx_pin=1;  rx_port=5;  rx_pin=2;  psel=0x05U; break;
        case UART6: tx_port=5;  tx_pin=6;  rx_port=5;  rx_pin=5;  psel=0x04U; break;
        case UART7: tx_port=6;  tx_pin=13; rx_port=6;  rx_pin=14; psel=0x05U; break;
        case UART8: tx_port=10; tx_pin=0;  rx_port=6;  rx_pin=7;  psel=0x04U; break;
        case UART9: tx_port=1;  tx_pin=9;  rx_port=1;  rx_pin=10; psel=0x05U; break;
        default: return;
    }

    PWPR = 0x00U;   /* Step 1: clear B0WI  (allows changing PFSWE) */
    PWPR = 0x40U;   /* Step 2: set  PFSWE  (allows writing PFS)     */

    /* RX pin: peripheral function, input direction */
    PmnPFS(rx_port, rx_pin)  = PmnPFS_PSEL(psel);
    PmnPFS(rx_port, rx_pin) |= PmnPFS_PMR;
    PORT_PDR(rx_port) &= (uint16_t)(~(uint16_t)(1U << rx_pin));

    /* TX pin: peripheral function, output direction */
    PmnPFS(tx_port, tx_pin)  = PmnPFS_PSEL(psel);
    PmnPFS(tx_port, tx_pin) |= PmnPFS_PMR;
    PORT_PDR(tx_port) |= (uint16_t)(1U << tx_pin);

    PWPR = 0x00U;   /* Step 3: clear PFSWE */
    PWPR = 0x80U;   /* Step 4: set  B0WI   (locks PFS writes) */
}

/* -----------------------------------------------------------------------
 * UART_Init — initialise one SCI channel in async UART mode.
 *
 * BRR formula with SEMR: BGDM=1 (bit6), ABCS=1 (bit4) → divisor coefficient 8:
 *   BRR = SCI_PCLK_HZ / (8 × baudrate) − 1
 * ----------------------------------------------------------------------- */
void UART_Init(UART_t uart, uint32_t baudrate)
{
    uint8_t n = (uint8_t)uart;
    if (n > 9U) { return; }

    uart_clock_init(n);
    uart_pin_config(uart);

    SCI_SCR(n)  = 0x00U;                                        /* disable TX/RX while configuring */
    SCI_SMR(n)  = 0x00U;                                        /* async, 8-bit, no parity, 1 stop */
    SCI_SEMR(n) = (uint8_t)(SEMR_BGDM | SEMR_ABCS);            /* BGDM=1(b6), ABCS=1(b4) → /8    */
    
    uint32_t divisor = 8UL * baudrate;
    SCI_BRR(n)  = (uint8_t)((SCI_PCLK_HZ + (divisor / 2U)) / divisor - 1U);

    /* BRR settling wait — RA6M5 §30.2.20: BRR must be written with TE=RE=0,
     * and the baud rate generator needs at least 1 bit period to settle before
     * the first transmission.  At 100 MHz / 115200 baud: 1 bit ≈ 868 cycles.
     * 1000 NOPs is conservative and covers all supported baud rates.        */
    for (volatile uint32_t brr_wait = 0U; brr_wait < 1000U; brr_wait++)
    {
        __asm volatile("nop");
    }

    SCI_SCR(n)  = (uint8_t)(SCR_TE | SCR_RE);                   /* enable TX and RX                */
}

/* -----------------------------------------------------------------------
 * UART_SendChar — blocking transmit of one byte, with timeout.
 *
 * S-05: timeout prevents infinite hang if TX is stuck (e.g. no pull-up on TX
 * line). On timeout the byte is silently dropped; caller is not blocked.
 * Hardware clears TDRE automatically on TDR write — no manual clear needed.
 * ----------------------------------------------------------------------- */
void UART_SendChar(UART_t uart, char data)
{
    uint8_t  n  = (uint8_t)uart;
    uint32_t to = DRV_TIMEOUT_TICKS;
    if (n > 9U) { return; }

    while (!(SCI_SSR(n) & SSR_TDRE))
    {
        if (--to == 0U) { return; }   /* timeout: drop byte, avoid hang */
    }
    SCI_TDR(n) = (uint8_t)data;
}

/* -----------------------------------------------------------------------
 * UART_SendString — transmit a null-terminated string.
 * ----------------------------------------------------------------------- */
void UART_SendString(UART_t uart, const char *str)
{
    while (*str != '\0')
    {
        UART_SendChar(uart, *str++);
    }
}

/* -----------------------------------------------------------------------
 * UART_ReceiveChar — blocking receive of one byte, with timeout.
 *
 * S-05: timeout prevents infinite hang if no data arrives.
 * Returns 0 on timeout (not a valid character in most protocols;
 * caller should check independently if reliable error detection is needed).
 * ----------------------------------------------------------------------- */
char UART_ReceiveChar(UART_t uart)
{
    uint8_t  n  = (uint8_t)uart;
    uint32_t to = DRV_TIMEOUT_TICKS;
    if (n > 9U) { return 0; }

    while (!(SCI_SSR(n) & SSR_RDRF))
    {
        if (--to == 0U) { return 0; }   /* timeout: return 0 */
    }
    return (char)SCI_RDR(n);
}