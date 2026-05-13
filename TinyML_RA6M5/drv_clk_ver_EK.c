#include "drv_clk.h"
#include "drv_rwp.h"

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
static void clk_start_pll2_for_usb(void)
{
    PLL2CCR = (uint16_t)(((uint16_t)PLL2_MUL_X20 << PLLCCR_PLLMUL_POS)
            | ((uint16_t)PLL2_SOURCE_MOSC << PLLCCR_PLSRCSEL_POS)
            | (uint16_t)PLL2_DIV_2);
    PLL2CR &= (uint8_t)~PLL2CR_PLL2STP;
    while ((OSCSF & OSCSF_PLL2SF) == 0U)
    {
        __asm volatile ("nop");
    }
}

static void clk_configure_usbfs_clock(void)
{
    USBCKCR = (uint8_t)((USBCKCR & USBCKCR_USBCKSEL_MASK) | USBCKCR_USBCKSREQ);
    while ((USBCKCR & USBCKCR_USBCKSRDY) == 0U)
    {
        __asm volatile ("nop");
    }

    USBCKDIVCR = USBCKDIV_5;
    USBCKCR = (uint8_t)(USBCKSEL_PLL2 | USBCKCR_USBCKSREQ);
    USBCKCR = USBCKSEL_PLL2;

    while ((USBCKCR & USBCKCR_USBCKSRDY) != 0U)
    {
        __asm volatile ("nop");
    }
}

void CLK_Init(void)
{
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
    while ((OSCSF & OSCSF_MOSCSF) == 0U)
    {
        __asm volatile ("nop");
    }

    clk_start_pll2_for_usb();

    /* XTAL / 3 * 25.0 = 200 MHz, per configuration.xml and RA6M5 PLL type 1 encoding. */
    PLLCCR = (uint16_t)(((uint16_t)PLL_MUL_X25 << PLLCCR_PLLMUL_POS)
           | ((uint16_t)PLL_SOURCE_MOSC << PLLCCR_PLSRCSEL_POS)
           | (uint16_t)PLL_DIV_3);
    PLLCR &= (uint8_t)~PLLCR_PLLSTP;
    while ((OSCSF & OSCSF_PLLSF) == 0U)
    {
        __asm volatile ("nop");
    }

    SCKDIVCR = ((uint32_t)SCKDIV_1 << SCKDIVCR_ICKPOS)   /* ICLK  = 200 MHz */
             | ((uint32_t)SCKDIV_2 << SCKDIVCR_PCKAPOS)  /* PCLKA = 100 MHz */
             | ((uint32_t)SCKDIV_4 << SCKDIVCR_PCKBPOS)  /* PCLKB =  50 MHz */
             | ((uint32_t)SCKDIV_4 << SCKDIVCR_PCKCPOS)  /* PCLKC =  50 MHz */
             | ((uint32_t)SCKDIV_2 << SCKDIVCR_PCKDPOS)  /* PCLKD = 100 MHz */
             | ((uint32_t)SCKDIV_2 << SCKDIVCR_BCKPOS)   /* BCLK  = 100 MHz */
             | ((uint32_t)SCKDIV_4 << SCKDIVCR_FCKPOS);  /* FCLK  =  50 MHz */

    SCKSCR = SCKSCR_PLL;
    clk_configure_usbfs_clock();

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