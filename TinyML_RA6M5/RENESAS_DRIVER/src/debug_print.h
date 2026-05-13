/**
 * @file    debug_print.h
 * @brief   Portable printf-like debug output with selectable backend.
 *
 * Configure in Config/rtos_config.h:
 *
 *   OS_DEBUG_ENABLE           — 1: output active, 0: all calls compile away
 *   OS_DEBUG_BACKEND_UART     — bare-metal SCI UART (default)
 *   OS_DEBUG_BACKEND_SEMIHOST — ARM semihosting over JTAG/SWD
 *   OS_DEBUG_UART_CHANNEL     — SCI channel number (0–9)
 *   OS_DEBUG_UART_BAUDRATE    — baud rate (e.g. 115200)
 *
 * Usage:
 *   debug_print_init();                       // call once before use
 *   if (debug_print_backend_ready() == 0) { ... }
 *   debug_print("val=%d, str=%s\r\n", 42, "ok");
 *
 * UART backend prefixes each message with "[<tick> ms] ".
 * Before the kernel starts, the tick value remains 0.
 *
 * Supported format specifiers: %s %d %u %x %X %c %%
 */

#ifndef DEBUG_PRINT_H
#define DEBUG_PRINT_H

#include "rtos_config.h"

/**
 * OS_DEBUG_BACKEND_USB_CDC — route debug_print() to the USB FS CDC BULK IN
 * pipe instead of the UART.
 *
 *   0 = disabled (default — UART SCI backend active)
 *   1 = USB CDC fire-and-forget log path
 *
 * Only one backend may be active at a time (UART or USB CDC).
 */
#ifndef OS_DEBUG_BACKEND_USB_CDC
#define OS_DEBUG_BACKEND_USB_CDC  0
#endif

#if OS_DEBUG_BACKEND_UART && OS_DEBUG_BACKEND_USB_CDC
#error "debug_print: select only one backend (UART or USB_CDC)"
#endif

#if OS_DEBUG_ENABLE

  #if OS_DEBUG_BACKEND_SEMIHOST
    /* --- Semihosting backend -------------------------------------------- */
    /* Requires: --specs=rdimon.specs in linker flags.                       */
    #include <stdio.h>
    static inline void debug_print_init(void) { /* nothing needed */ }
    static inline unsigned char debug_print_backend_ready(void) { return 1U; }
    #define debug_print(fmt, ...)  printf(fmt, ##__VA_ARGS__)

  #elif OS_DEBUG_BACKEND_USB_CDC
    /* --- USB CDC backend ----------------------------------------------- */
    /* debug_print_init() owns USB bring-up + host-open wait before main continues. */
    void debug_print_init(void);
    unsigned char debug_print_backend_ready(void);
    void debug_print(const char *fmt, ...);

  #else
    /* --- UART backend (default) ----------------------------------------- */
    /* Bare-metal SCI transmit; no heap, no stdio dependency.                */
    void debug_print_init(void);
    unsigned char debug_print_backend_ready(void);
    void debug_print(const char *fmt, ...);

  #endif /* OS_DEBUG_BACKEND_SEMIHOST / OS_DEBUG_BACKEND_USB_CDC */

#else  /* OS_DEBUG_ENABLE == 0 -------------------------------------------- */
  /* Strip everything: zero code, zero data, no warnings about unused args. */
  static inline void debug_print_init(void) { }
  static inline unsigned char debug_print_backend_ready(void) { return 1U; }
  #define debug_print(fmt, ...)  do { } while (0)

#endif /* OS_DEBUG_ENABLE */

#endif /* DEBUG_PRINT_H */
