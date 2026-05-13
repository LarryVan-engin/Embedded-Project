# FW_Clock_Driver

Tags: #done #firmware #clock

Clock and module stop control driver for RA6M5. Files: `Driver/Include/drv_clk.h`, `Driver/Source/drv_clk.c`. Hardware constraints: [[HW_RA6M5_ClockTree]]. Write protection: [[FW_RWP_Driver]]. Formerly split across `CGC.h` (misnamed) and `LPM.h` (misnamed — contained MSTPCR, not LPM). Reorganized in Phase 5.

**Board Support**: Unified configuration for CK-RA6M5 and EK-RA6M5 via `board_config.h`. Set `BOARD_TYPE` to switch between boards — all clock initialization and fallback logic is identical (200 MHz PLL with HOCO 48 MHz fallback).

---

## API

```c
void CLK_Init(void);
void CLK_ModuleStart_SCI(SCI_t peripheral);
```

---

## CLK_Init

Configures the production clock tree used by the current firmware. Called from `Reset_Handler` in `startup.c` before `main()`.

```c
void CLK_Init(void)
{
    RWP_Unlock_Clock_MSTP();

    FLWT = 3;                 /* flash wait states for 200 MHz */
    SRAMWTSC = 1;             /* SRAM wait state for 200 MHz    */

    MOMCR    = 0x00;          /* crystal, normal drive */
    MOSCWTCR = 9;             /* main osc stabilization setting */
    MOSCCR   = 0x00;          /* start MOSC */
    while (!(OSCSF & OSCSF_MOSCSF)) { }

    PLLCCR = (49U << 8) | 0x02U;  /* XTAL /3 * 25.0 = 200 MHz */
    PLLCR  = 0x00U;               /* start PLL */
    while (!(OSCSF & OSCSF_PLLSF)) { }

    SCKDIVCR = ...;           /* ICLK=200, PCLKA=100, PCLKB=50, PCLKC=50, PCLKD=100, BCLK=100, FCLK=50 */
    SCKSCR   = SCKSCR_PLL;

    RWP_Lock_Clock_MSTP();
}
```

Result:
- ICLK  = 200 MHz
- PCLKA = 100 MHz
- PCLKB = 50 MHz
- PCLKC = 50 MHz
- PCLKD = 100 MHz
- BCLK  = 100 MHz
- FCLK  = 50 MHz

See [[HW_RA6M5_ClockTree]].

### Fallback Mode (on PLL/MOSC failure)

If main oscillator (MOSC) or PLL fails to stabilize within `CLK_WAIT_TIMEOUT_LOOPS`, the driver automatically falls back to HOCO (internal oscillator):

```c
static void clk_fallback_to_hoco(void)
{
    /* Switch to HOCO 48 MHz */
    HOCOCR &= ~MOSCCR_MOSTP;
    SCKDIVCR = all /1 dividers;  /* All clocks = 48 MHz */
    SCKSCR = SCKSCR_HOCO;
    
    g_actual_pclk_hz = 48000000UL;
    g_actual_iclk_hz = 48000000UL;
    g_clk_fallback_happened = 1U;
    
    /* Signal fallback via LED3 (board-specific from board_config.h) */
    GPIO_Write_Pin((GPIO_PORT_t)LED3_PORT, LED3_PIN, GPIO_PIN_SET);
}
```

For debugging, check:
- `CLK_GetFallbackOccurred()` returns 1 if fallback occurred
- `CLK_GetActualICLK()` returns 200000000 (success) or 48000000 (fallback)
- `CLK_GetActualSCIClock()` returns 50000000 (success) or 48000000 (fallback)
- LED3 toggles once on boot if fallback occurred

---

## CLK_ModuleStart_SCI

Clears the module stop bit for one SCI channel. Replaces the misnamed `LPM_Unlock()`. Wraps the MSTPCRB write with PRCR unlock/lock via [[FW_RWP_Driver]].

MSTPCRB mapping: SCI0=bit31, SCI1=bit30, ..., SCI9=bit22. Clearing the bit enables the module clock.

Called from `uart_clock_init()` (static, inside `drv_uart.c`) during `UART_Init`.

---

## MSTPCR Registers

Located at `0x40084000` base (MSTPCRA/B/C/D). Hardware detail: [[HW_RA6M5_ClockTree]] §9.3.3.

RIIC module stop is also released here (MSTPCRB bits 9/8/7), by `i2c_clock_init()` (static, inside `drv_i2c.c`).

---

## File Structure Note

`drv_clk.h` merged what were previously two misnamed headers:
- `CGC.h` — contained clock generation registers
- `LPM.h` — contained MSTPCR + SCI_t enum (no LPM logic at all)

Old headers are now forwarding stubs: `#include "drv_clk.h"`.

---

## Board Configuration Integration

**Files involved**:
- `Driver/Include/board_config.h` — Master configuration (set `BOARD_TYPE` here)
- `Driver/Include/rtos_config.h` — Sources debug timing from board config
- `Config/rtos_config.h` — Includes board_config.h for unified setup

**Key**: LED3 fallback signal uses board-specific pin from `board_config.h`:

```c
/* CK-RA6M5 */
#define LED3_PORT  6
#define LED3_PIN   9   /* P609 */

/* EK-RA6M5 */
#define LED3_PORT  0
#define LED3_PIN   8   /* P008 */
```

Clock initialization is **identical** on both boards; only the fallback LED signal differs by board.

**How to switch boards**: Edit `board_config.h`, line 19:
```c
#define BOARD_TYPE      BOARD_TYPE_CK    // or BOARD_TYPE_EK
```
Then rebuild — all drivers adapt automatically.
