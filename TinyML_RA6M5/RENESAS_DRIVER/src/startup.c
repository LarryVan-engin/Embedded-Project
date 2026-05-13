/*
 * startup.c  —  Bare-metal startup for Renesas RA6M5 (R7FA6M5BH, Cortex-M33)
 *
 * Provides:
 *   - Main stack (4 KB, in .bss.g_main_stack — matches linker script)
 *   - Core exception vector table (.fixed_vectors)
 *   - External IRQ vector table (.application_vectors, all default handlers)
 *   - Reset_Handler: enables FPU, copies .data, zeros .bss, calls main()
 */

#include <stdint.h>
#include "drv_clk.h"

/* -----------------------------------------------------------------------
 * Stack  (linker script places .bss.g_main_stack in noinit RAM)
 * ----------------------------------------------------------------------- */
#define STARTUP_STACK_SIZE  (4096U)    /* 4 KB — adjust if needed */

static uint32_t g_main_stack[STARTUP_STACK_SIZE / sizeof(uint32_t)]
    __attribute__((section(".bss.g_main_stack"), used, aligned(8)));

/* -----------------------------------------------------------------------
 * Linker-exported startup symbols  (defined via PROVIDE in fsp_gen.ld)
 * ----------------------------------------------------------------------- */
extern uint32_t _sdata;     /* Start of .data in RAM        */
extern uint32_t _edata;     /* End   of .data in RAM        */
extern uint32_t _sidata;    /* LMA   of .data in Flash      */
extern uint32_t _sbss;      /* Start of .bss  in RAM        */
extern uint32_t _ebss;      /* End   of .bss  in RAM        */

/* C++ global constructor array (may be empty for pure-C builds) */
extern void (* const __init_array_start[])(void);
extern void (* const __init_array_end  [])(void);

/* -----------------------------------------------------------------------
 * Forward declarations
 * ----------------------------------------------------------------------- */
void Reset_Handler(void);
void Default_Handler(void);
int  main(void);

/* Core exception handler weak aliases */
void NMI_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void SecureFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)      __attribute__((weak, alias("Default_Handler")));

typedef void (* const VectorEntry_t)(void);

/* -----------------------------------------------------------------------
 * Core (fixed) exception vector table — placed at Flash 0x00000000
 * Entry 0: initial MSP value (top of stack)
 * -----------------------------------------------------------------------*/
__attribute__((section(".fixed_vectors"), used))
static const VectorEntry_t g_fixed_vectors[] =
{
    /* Initial stack pointer: top of g_main_stack */
    (VectorEntry_t)(uint32_t)(&g_main_stack[STARTUP_STACK_SIZE / sizeof(uint32_t)]),

    Reset_Handler,          /*  1  Reset             */
    NMI_Handler,            /*  2  NMI               */
    HardFault_Handler,      /*  3  Hard Fault        */
    MemManage_Handler,      /*  4  MemManage Fault   */
    BusFault_Handler,       /*  5  Bus Fault         */
    UsageFault_Handler,     /*  6  Usage Fault       */
    SecureFault_Handler,    /*  7  Secure Fault (M33)*/
    0,                      /*  8  Reserved          */
    0,                      /*  9  Reserved          */
    0,                      /* 10  Reserved          */
    SVC_Handler,            /* 11  SVCall            */
    DebugMon_Handler,       /* 12  Debug Monitor     */
    0,                      /* 13  Reserved          */
    PendSV_Handler,         /* 14  PendSV            */
    SysTick_Handler,        /* 15  SysTick           */
};

/* -----------------------------------------------------------------------
 * Application (external IRQ) vector table — RA6M5 has 96 external IRQs.
 * All filled with Default_Handler; override by defining the function
 * with the same name in user code (weak alias will be overridden).
 * ----------------------------------------------------------------------- */
__attribute__((section(".application_vectors"), used))
static const VectorEntry_t g_application_vectors[96] =
{
    [0 ... 95] = Default_Handler,
};

/* -----------------------------------------------------------------------
 * Default handler — infinite loop so debugger can catch unhandled events
 * ----------------------------------------------------------------------- */
void Default_Handler(void)
{
    /* Set breakpoint here to identify the unhandled exception/IRQ */
    while (1)
    {
        __asm volatile ("nop");
    }
}

/* -----------------------------------------------------------------------
 * Reset_Handler — runs immediately after reset
 * ----------------------------------------------------------------------- */
void Reset_Handler(void)
{
    /* 1. Enable the FPU (Cortex-M33 with FPV5-SP FPU)
     *    SCB->CPACR @ 0xE000ED88: set CP10 and CP11 to full access (0b11)  */
    *(volatile uint32_t *)0xE000ED88U |= (0xFU << 20);
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");

    /* 2. Copy initialised data segment from Flash (LMA) to RAM (VMA) */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata)
    {
        *dst++ = *src++;
    }

    /* 3. Zero-fill the BSS segment */
    dst = &_sbss;
    while (dst < &_ebss)
    {
        *dst++ = 0U;
    }

    /* 4. Call C++ global constructors (no-op for pure-C builds) */
    for (void (** fn)(void) = (void (**)(void))__init_array_start;
         fn < (void (**)(void))__init_array_end;
         fn++)
    {
        (*fn)();
    }

    /* 5. Configure system clock: explicit MOCO 8 MHz (idempotent) */
    CLK_Init();

    /* 6. Call application entry */
    main();

    /* Should never reach here */
    while (1)
    {
        __asm volatile ("nop");
    }
}
