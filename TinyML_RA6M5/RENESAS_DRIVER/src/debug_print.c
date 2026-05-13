/**
 * @file    debug_print.c
 * @brief   UART backend for debug_print() — bare-metal SCI transmit.
 *
 * Only compiled when OS_DEBUG_ENABLE=1 and OS_DEBUG_BACKEND_UART=1
 * (see Config/rtos_config.h).  No heap, no stdio dependency.
 *
 * Supported format specifiers: %s %d %u %x %X %c %%
 * Width/precision/length modifiers are not supported (not needed for
 * embedded diagnostics; keeps code size minimal).
 */

#include "debug_print.h"
#include "rtos_config.h"

#if OS_DEBUG_ENABLE && !OS_DEBUG_BACKEND_SEMIHOST && !OS_DEBUG_BACKEND_USB_CDC

#include "drv_uart.h"
#include "kernel.h"
#include <stdarg.h>
#include <stdint.h>

static uint8_t s_dbg_backend_ready = 0U;

/* Channel used for all output (value from rtos_config.h). */
#define DBG_CH  ((UART_t)OS_DEBUG_UART_CHANNEL)

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static void dbg_putc(char c)
{
    UART_SendChar(DBG_CH, c);
}

/** Transmit a null-terminated string. */
static void dbg_puts(const char *s)
{
    if (s == (void *)0) { s = "(null)"; }
    while (*s != '\0') { dbg_putc(*s++); }
}

/**
 * Transmit an unsigned integer in the given base (10 or 16).
 * Upper-case hex when @p upper != 0.
 */
static void dbg_putu(uint32_t val, uint8_t base, uint8_t upper)
{
    /* 10 digits enough for UINT32_MAX in decimal; 8 in hex. */
    char    buf[10];
    uint8_t i = 0U;

    if (val == 0U) { dbg_putc('0'); return; }

    while (val > 0U && i < (uint8_t)sizeof(buf))
    {
        uint8_t digit = (uint8_t)(val % (uint32_t)base);
        if (digit < 10U) {
            buf[i] = (char)('0' + digit);
        } else {
            buf[i] = upper ? (char)('A' + digit - 10U)
                           : (char)('a' + digit - 10U);
        }
        i++;
        val /= (uint32_t)base;
    }

    /* Digits were accumulated least-significant first; reverse-transmit. */
    while (i > 0U) { dbg_putc(buf[--i]); }
}

static void dbg_put_timestamp(void)
{
    dbg_putc('[');
    dbg_putu(OS_GetTick(), 10U, 0U);
    dbg_puts(" ms] ");
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * debug_print_init — initialise the debug UART.
 * Must be called once before any debug_print() call.
 */
void debug_print_init(void)
{
    UART_Init(DBG_CH, OS_DEBUG_UART_BAUDRATE);
    s_dbg_backend_ready = (SCI_SSR(OS_DEBUG_UART_CHANNEL) & SSR_TDRE) ? 1U : 0U;
}

uint8_t debug_print_backend_ready(void)
{
    return s_dbg_backend_ready;
}

/**
 * debug_print — printf-like transmit over the debug UART.
 *
 * Supported specifiers:
 *   %s — null-terminated string  (%s of NULL prints "(null)")
 *   %d — signed 32-bit decimal
 *   %u — unsigned 32-bit decimal
 *   %x — unsigned 32-bit hex, lower-case
 *   %X — unsigned 32-bit hex, upper-case
 *   %c — single character
 *   %% — literal '%'
 */
void debug_print(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    dbg_put_timestamp();

    for (const char *p = fmt; *p != '\0'; p++)
    {
        if (*p != '%')
        {
            dbg_putc(*p);
            continue;
        }

        p++;  /* step past '%' */

        switch (*p)
        {
            case 's':
                dbg_puts(va_arg(args, const char *));
                break;

            case 'd': {
                int32_t v = va_arg(args, int32_t);
                if (v < 0) { dbg_putc('-'); dbg_putu((uint32_t)(-v), 10U, 0U); }
                else        {               dbg_putu((uint32_t)  v,  10U, 0U); }
                break;
            }

            case 'u':
                dbg_putu(va_arg(args, uint32_t), 10U, 0U);
                break;

            case 'x':
                dbg_putu(va_arg(args, uint32_t), 16U, 0U);
                break;

            case 'X':
                dbg_putu(va_arg(args, uint32_t), 16U, 1U);
                break;

            case 'c':
                dbg_putc((char)va_arg(args, int));
                break;

            case '%':
                dbg_putc('%');
                break;

            case '\0':
                /* Trailing '%' at end of format string — stop. */
                goto done;

            default:
                /* Unknown specifier: pass through literally. */
                dbg_putc('%');
                dbg_putc(*p);
                break;
        }
    }

done:
    va_end(args);
}

#endif /* OS_DEBUG_ENABLE && !OS_DEBUG_BACKEND_SEMIHOST && !OS_DEBUG_BACKEND_USB_CDC */

/* =========================================================================
 * USB CDC backend
 * =========================================================================
 * Compiled when OS_DEBUG_ENABLE=1 and OS_DEBUG_BACKEND_USB_CDC=1.
 * Sends each debug_print() message once over USB_Dev_Write() so the output
 * shape matches UART and avoids duplicate/truncated fallback frames.
 * ========================================================================= */

#if OS_DEBUG_ENABLE && OS_DEBUG_BACKEND_USB_CDC

#include "drv_usb.h"
#include "kernel.h"
#include <stdarg.h>
#include <stdio.h>

static uint8_t s_dbg_backend_ready = 0U;
#define DBG_USB_HOST_READY_TIMEOUT_MS 3000U

static void debug_delay_ms_bm(uint32_t ms)
{
    volatile uint32_t n = ms * 100000U;
    while (n-- != 0U)
    {
        __asm volatile("nop");
    }
}

void debug_print_init(void)
{
    uint32_t waited_ms = 0U;

    if (USB_Init(USB_MODE_DEVICE_CDC) != DRV_OK)
    {
        s_dbg_backend_ready = 0U;
        return;
    }

    while ((USB_Dev_IsHostReady() == 0U) && (waited_ms < DBG_USB_HOST_READY_TIMEOUT_MS))
    {
        USB_PollEvents();
        debug_delay_ms_bm(1U);
        waited_ms++;
    }

    /* Do not block forever when host is not connected yet. */
    s_dbg_backend_ready = (USB_Dev_IsHostReady() != 0U) ? 1U : 0U;
}

uint8_t debug_print_backend_ready(void)
{
    return s_dbg_backend_ready;
}

static volatile uint8_t s_usb_dbg_lock = 0U;

static uint8_t usb_dbg_try_lock(void)
{
    uint32_t tries;

    for (tries = 0U; tries < 10000U; tries++)
    {
        OS_EnterCritical();
        if (s_usb_dbg_lock == 0U)
        {
            s_usb_dbg_lock = 1U;
            OS_ExitCritical();
            return 1U;
        }
        OS_ExitCritical();
    }

    return 0U;
}

static void usb_dbg_unlock(void)
{
    OS_EnterCritical();
    s_usb_dbg_lock = 0U;
    OS_ExitCritical();
}

void debug_print(const char *fmt, ...)
{
    char out[256];
    uint32_t tick;
    int n_prefix;
    int n_body;
    int n_total;
    size_t body_cap;
    va_list ap;

    if (fmt == NULL)
    {
        return;
    }

    if (s_dbg_backend_ready == 0U)
    {
        USB_PollEvents();
        if (USB_Dev_IsHostReady() == 0U)
        {
            return;
        }
        s_dbg_backend_ready = 1U;
    }

    if (usb_dbg_try_lock() == 0U)
    {
        return;
    }

    tick = OS_GetTick();
    n_prefix = snprintf(out, sizeof(out), "[%lu ms] ", (unsigned long)tick);
    if (n_prefix < 0)
    {
        usb_dbg_unlock();
        return;
    }
    if ((size_t)n_prefix >= sizeof(out))
    {
        usb_dbg_unlock();
        return;
    }

    body_cap = sizeof(out) - (size_t)n_prefix;

    va_start(ap, fmt);
    n_body = vsnprintf(&out[n_prefix], body_cap, fmt, ap);
    va_end(ap);

    if (n_body < 0)
    {
        usb_dbg_unlock();
        return;
    }

    if ((size_t)n_body >= body_cap)
    {
        n_body = (int)(body_cap - 1U);
    }

    n_total = n_prefix + n_body;
    if (n_total > 0)
    {
        (void)USB_Dev_Write((const uint8_t *)out, (uint32_t)n_total);
    }

    usb_dbg_unlock();
}

#endif /* OS_DEBUG_ENABLE && OS_DEBUG_BACKEND_USB_CDC */
