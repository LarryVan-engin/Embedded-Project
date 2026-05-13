#include "drv_clk.h"
#include "drv_rwp.h"
#include "board_config.h"
#include "GPIO.h"

/*
 * CK production mode with robust fallback:
 * 1 = force safe HOCO-only startup
 * 0 = use full production clock tree with timeout/fallback protection
 */
#define CLK_SAFE_HOCO_ONLY 0U
#define CLK_WAIT_TIMEOUT_LOOPS 2000000UL

/* Global to track actual SCI source clock after CLK_Init (PCLKB for this project). */
uint32_t g_actual_pclk_hz = 50000000UL;  /* default 50 MHz in production mode */

/* Global to track actual ICLK after CLK_Init (used by SysTick for timing calculation) */
uint32_t g_actual_iclk_hz = 200000000UL;  /* default 200 MHz if production mode succeeds */

static uint8_t g_clk_fallback_happened = 0U;

/* -----------------------------------------------------------------------
 * CLK_Init — bring the current project to its configured 200 MHz state.
 *
 * Source of truth for the system clocks:
 *   configuration.xml
 *   - XTAL   = 24 MHz crystal
 *   - PLL    = XTAL / 3 * 25.0 = 200 MHz
 *   - ICLK   = 200 MHz
 *   - PCLKA  = 100 MHz
 *   - PCLKB  =  50 MHz
 *   - PCLKC  =  50 MHz
 *   - PCLKD  = 100 MHz
 *   - BCLK   = 100 MHz
 *   - FCLK   =  50 MHz
 *
 * USBFS on RA6M5 also needs a dedicated 48 MHz UCLK.  We derive that from
 * PLL2 = XTAL / 2 * 20.0 = 240 MHz, then UCLK = PLL2 / 5 = 48 MHz.
 *
 * The RA6M5 requires flash and SRAM wait states before raising ICLK.
 * Those settings are mirrored from the FSP-generated reference project.
 * ----------------------------------------------------------------------- */
static uint8_t clk_wait_flag_set(volatile uint8_t * reg, uint8_t mask, uint32_t timeout)
{
    while (timeout-- > 0UL)
    {
        if ((*reg & mask) != 0U)
        {
            return 1U;
        }
        __asm volatile ("nop");
    }

    return 0U;
}

static uint8_t clk_wait_flag_clear(volatile uint8_t * reg, uint8_t mask, uint32_t timeout)
{
    while (timeout-- > 0UL)
    {
        if ((*reg & mask) == 0U)
        {
            return 1U;
        }
        __asm volatile ("nop");
    }

    return 0U;
}

static void clk_fallback_to_hoco(void)
{
    HOCOCR &= (uint8_t)~MOSCCR_MOSTP;
    (void)clk_wait_flag_set(&OSCSF, OSCSF_HOCOSF, CLK_WAIT_TIMEOUT_LOOPS);

    SCKDIVCR = ((uint32_t)SCKDIV_1 << SCKDIVCR_ICKPOS)
             | ((uint32_t)SCKDIV_1 << SCKDIVCR_PCKAPOS)
             | ((uint32_t)SCKDIV_1 << SCKDIVCR_PCKBPOS)
             | ((uint32_t)SCKDIV_1 << SCKDIVCR_PCKCPOS)
             | ((uint32_t)SCKDIV_1 << SCKDIVCR_PCKDPOS)
             | ((uint32_t)SCKDIV_1 << SCKDIVCR_BCKPOS)
             | ((uint32_t)SCKDIV_1 << SCKDIVCR_FCKPOS);
    SCKSCR = SCKSCR_HOCO;
    
    /* Update global PCLK and ICLK values: HOCO 48 MHz with /1 divider */
    g_actual_pclk_hz = 48000000UL;
    g_actual_iclk_hz = 48000000UL;  /* ICLK also 48 MHz when on HOCO */
    g_clk_fallback_happened = 1U;
    
    /* Signal fallback on LED3: toggle once to indicate fallback occurred */
    GPIO_Write_Pin((GPIO_PORT_t)LED3_PORT, LED3_PIN, GPIO_PIN_SET);
}

static uint8_t clk_start_pll2_for_usb(void)
{
    PLL2CCR = (uint16_t)(((uint16_t)PLL2_MUL_X20 << PLLCCR_PLLMUL_POS)
            | ((uint16_t)PLL2_SOURCE_MOSC << PLLCCR_PLSRCSEL_POS)
            | (uint16_t)PLL2_DIV_2);
    PLL2CR &= (uint8_t)~PLL2CR_PLL2STP;
    return clk_wait_flag_set(&OSCSF, OSCSF_PLL2SF, CLK_WAIT_TIMEOUT_LOOPS);
}

static uint8_t clk_configure_usbfs_clock(void)
{
    USBCKCR = (uint8_t)((USBCKCR & USBCKCR_USBCKSEL_MASK) | USBCKCR_USBCKSREQ);
    if (clk_wait_flag_set(&USBCKCR, USBCKCR_USBCKSRDY, CLK_WAIT_TIMEOUT_LOOPS) == 0U)
    {
        return 0U;
    }

    USBCKDIVCR = USBCKDIV_5;
    USBCKCR = (uint8_t)(USBCKSEL_PLL2 | USBCKCR_USBCKSREQ);
    USBCKCR = USBCKSEL_PLL2;

    if (clk_wait_flag_clear(&USBCKCR, USBCKCR_USBCKSRDY, CLK_WAIT_TIMEOUT_LOOPS) == 0U)
    {
        return 0U;
    }

    return 1U;
}

/* Return 1 if fallback to HOCO occurred, 0 if system is on production clock */
uint8_t CLK_GetFallbackOccurred(void)
{
    return g_clk_fallback_happened;
}

/* Get the actual ICLK frequency (200 MHz production, 48 MHz fallback) */
uint32_t CLK_GetActualICLK(void)
{
    return g_actual_iclk_hz;
}

/* Get the actual SCI clock used by UART BRR calculation (PCLKB domain). */
uint32_t CLK_GetActualSCIClock(void)
{
    return g_actual_pclk_hz;
}

void CLK_Init(void)
{
#if CLK_SAFE_HOCO_ONLY
    RWP_Unlock_Clock_MSTP();

    /* Ensure HOCO is running and switch system clock to HOCO safely. */
    HOCOCR &= (uint8_t)~MOSCCR_MOSTP;
    (void)clk_wait_flag_set(&OSCSF, OSCSF_HOCOSF, CLK_WAIT_TIMEOUT_LOOPS);

    /* Keep all bus dividers at /1 for a simple, robust bring-up path. */
    SCKDIVCR = ((uint32_t)SCKDIV_1 << SCKDIVCR_ICKPOS)
             | ((uint32_t)SCKDIV_1 << SCKDIVCR_PCKAPOS)
             | ((uint32_t)SCKDIV_1 << SCKDIVCR_PCKBPOS)
             | ((uint32_t)SCKDIV_1 << SCKDIVCR_PCKCPOS)
             | ((uint32_t)SCKDIV_1 << SCKDIVCR_PCKDPOS)
             | ((uint32_t)SCKDIV_1 << SCKDIVCR_BCKPOS)
             | ((uint32_t)SCKDIV_1 << SCKDIVCR_FCKPOS);

    SCKSCR = SCKSCR_HOCO;

    g_actual_pclk_hz = 48000000UL;
    g_actual_iclk_hz = 48000000UL;

    RWP_Lock_Clock_MSTP();
    return;
#endif

    RWP_Unlock_Clock_MSTP();

    /* 200 MHz on RA6M5 requires flash wait states before the switch. */
    FLWT = FLWT_3_WAIT;

    /* 200 MHz also requires SRAM wait states.  Unlock, update, relock. */
    __asm volatile ("dmb" ::: "memory");
    SRAMPRCR = SRAMPRCR_KEY_UNLOCK;
    __asm volatile ("dmb" ::: "memory");
    SRAMWTSC = SRAMWTSC_WAIT_1;
    __asm volatile ("dmb" ::: "memory");
    SRAMPRCR = SRAMPRCR_KEY_LOCK;
    __asm volatile ("dmb" ::: "memory");

    /* Configure main oscillator for a 24 MHz crystal and wait for stable MOSC. */
    MOMCR = 0x00U;
    MOSCWTCR = MOSCWTCR_MSTS_9;
    MOSCCR &= (uint8_t)~MOSCCR_MOSTP;
    if (clk_wait_flag_set(&OSCSF, OSCSF_MOSCSF, CLK_WAIT_TIMEOUT_LOOPS) == 0U)
    {
        clk_fallback_to_hoco();
        RWP_Lock_Clock_MSTP();
        return;
    }

    if (clk_start_pll2_for_usb() == 0U)
    {
        clk_fallback_to_hoco();
        RWP_Lock_Clock_MSTP();
        return;
    }

    /* XTAL / 3 * 25.0 = 200 MHz, per configuration.xml and RA6M5 PLL type 1 encoding. */
    PLLCCR = (uint16_t)(((uint16_t)PLL_MUL_X25 << PLLCCR_PLLMUL_POS)
           | ((uint16_t)PLL_SOURCE_MOSC << PLLCCR_PLSRCSEL_POS)
           | (uint16_t)PLL_DIV_3);
    PLLCR &= (uint8_t)~PLLCR_PLLSTP;
    if (clk_wait_flag_set(&OSCSF, OSCSF_PLLSF, CLK_WAIT_TIMEOUT_LOOPS) == 0U)
    {
        clk_fallback_to_hoco();
        RWP_Lock_Clock_MSTP();
        return;
    }

    SCKDIVCR = ((uint32_t)SCKDIV_1 << SCKDIVCR_ICKPOS)   /* ICLK  = 200 MHz */
             | ((uint32_t)SCKDIV_2 << SCKDIVCR_PCKAPOS)  /* PCLKA = 100 MHz */
             | ((uint32_t)SCKDIV_4 << SCKDIVCR_PCKBPOS)  /* PCLKB =  50 MHz */
             | ((uint32_t)SCKDIV_4 << SCKDIVCR_PCKCPOS)  /* PCLKC =  50 MHz */
             | ((uint32_t)SCKDIV_2 << SCKDIVCR_PCKDPOS)  /* PCLKD = 100 MHz */
             | ((uint32_t)SCKDIV_2 << SCKDIVCR_BCKPOS)   /* BCLK  = 100 MHz */
             | ((uint32_t)SCKDIV_4 << SCKDIVCR_FCKPOS);  /* FCLK  =  50 MHz */

    SCKSCR = SCKSCR_PLL;
    (void)clk_configure_usbfs_clock();

    /* Production clock tree: SCI source (PCLKB) = 50 MHz, ICLK = 200 MHz. */
    g_actual_pclk_hz = 50000000UL;
    /* Update ICLK to production value (fallback not occurred) */
    g_actual_iclk_hz = 200000000UL;  /* Production: ICLK = 200 MHz */

    RWP_Lock_Clock_MSTP();
}

/* -----------------------------------------------------------------------
 * CLK_ModuleStart_SCI — release module stop for one SCI channel.
 *
 * Replaces LPM_Unlock(). Wraps MSTPCRB write with PRCR protection.
 * MSTPCRB bit mapping: SCI0=bit31, SCI1=bit30, ..., SCI9=bit22
 * ----------------------------------------------------------------------- */
void CLK_ModuleStart_SCI(SCI_t peripheral)
{
    RWP_Unlock_Clock_MSTP();

    switch (peripheral)
    {
        case SCI0: MSTPCRB &= ~(1UL << 31U); break;
        case SCI1: MSTPCRB &= ~(1UL << 30U); break;
        case SCI2: MSTPCRB &= ~(1UL << 29U); break;
        case SCI3: MSTPCRB &= ~(1UL << 28U); break;
        case SCI4: MSTPCRB &= ~(1UL << 27U); break;
        case SCI5: MSTPCRB &= ~(1UL << 26U); break;
        case SCI6: MSTPCRB &= ~(1UL << 25U); break;
        case SCI7: MSTPCRB &= ~(1UL << 24U); break;
        case SCI8: MSTPCRB &= ~(1UL << 23U); break;
        case SCI9: MSTPCRB &= ~(1UL << 22U); break;
        default:   break;
    }

    RWP_Lock_Clock_MSTP();
}
