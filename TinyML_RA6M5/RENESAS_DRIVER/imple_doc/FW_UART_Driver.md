# FW_UART_Driver

Tags: #done #firmware #uart

UART driver for RA6M5 SCI peripheral in asynchronous mode. Files: `Driver/Include/drv_uart.h`, `Driver/Source/drv_uart.c`. Hardware constraints: [[HW_RA6M5_SCI]]. Clock source: [[FW_Clock_Driver]]. Bug fixes applied: [[RCA_UART_BRR_SEMR]], [[RCA_UART_SSR_Manual_Clear]].

**Board Support**: Unified configuration for CK-RA6M5 and EK-RA6M5 via `board_config.h`. Pin mappings and UART channel are set via `DEBUG_UART_CHANNEL` macro from board config. BRR divisor mode is selected via `DEBUG_UART_BRR_DIV`.

---

## API

```c
void UART_Init(UART_t uart, uint32_t baudrate);
void UART_SendChar(UART_t uart, char data);
void UART_SendString(UART_t uart, const char *str);
char UART_ReceiveChar(UART_t uart);
```

All busy-wait loops are protected by `DRV_TIMEOUT_TICKS`. On timeout: `SendChar` drops the byte silently; `ReceiveChar` returns 0.

---

## Initialization Sequence

```c
void UART_Init(UART_t uart, uint32_t baudrate)
{
    uint8_t n = (uint8_t)uart;
    uint32_t divisor, pclk, brr_val;
    
    if (n > 9U) { return; }

    uart_clock_init(n);        /* CLK_ModuleStart_SCI — releases MSTPCRB bit */
    uart_pin_config(uart);     /* configure TX/RX pins via PFS (board-aware) */
    
    SCI_SCR(n)  = 0x00;        /* disable TX/RX before config */
    SCI_SMR(n)  = 0x00;        /* async, 8-bit, no parity, 1 stop */

    /* Select BRR divisor mode based on board config */
#if (DEBUG_UART_BRR_DIV == 4U)
    divisor = 4UL * baudrate;
    SCI_SEMR(n) = (uint8_t)(SEMR_BGDM | SEMR_ABCS);  /* /4 mode */
#elif (DEBUG_UART_BRR_DIV == 8U)
    divisor = 8UL * baudrate;
    SCI_SEMR(n) = (uint8_t)(SEMR_BGDM);              /* /8 mode */
#endif
    
    /* Get actual PCLK (may be 48MHz if fallback occurred) */
    pclk = SCI_PCLK_HZ;  /* From FW_Clock_Driver globals */
    
    /* Calculate BRR with rounding: BRR = PCLK/(divisor*baudrate) - 1 */
    brr_val = (pclk + (divisor / 2U)) / divisor - 1U;
    SCI_BRR(n)  = (uint8_t)brr_val;

    /* BRR settling wait per RA6M5 §30.2.20 */
    for (volatile uint32_t brr_wait = 0U; brr_wait < 1000U; brr_wait++) {
        __asm volatile("nop");
    }

    SCI_SCR(n)  = (uint8_t)(SCR_TE | SCR_RE);  /* enable TX and RX */
}
```

### Key Improvements in Current Version

1. **Variable Declaration Order**: All variables declared at function start (C89/C90 compliance)
2. **Dynamic PCLK**: Uses `SCI_PCLK_HZ` global from [[FW_Clock_Driver]], handles 50 MHz (production) or 48 MHz (fallback)
3. **BRR Calculation**: Rounding formula `(pclk + divisor/2) / divisor - 1` for better accuracy
4. **SEMR Bit Positions** (from [[HW_RA6M5_SCI]] §34 Table 34.12):
   - BGDM = bit 6 (set for both /4 and /8 modes)
   - ABCS = bit 4 (set only for /4 mode)
5. **Board-Aware Pin Config**: `uart_pin_config()` uses board-specific pins from `board_config.h`

---

## BRR Calculation

BRR formula derivation from [[HW_RA6M5_SCI]]:

**Production Mode** (200 MHz PLL, PCLKB = 50 MHz):
```
Divisor mode /4: SCI_PCLK_HZ = 50 000 000
BRR = (50000000 + divisor/2) / divisor - 1
  where divisor = 4 * baudrate
```

**Fallback Mode** (48 MHz HOCO, all clocks = 48 MHz):
```
Divisor mode /4: SCI_PCLK_HZ = 48 000 000
BRR = (48000000 + divisor/2) / divisor - 1
```

### BRR Values

**Production Mode (PCLKB = 50 MHz)**:

| Baudrate | Divisor | BRR | Actual | Error |
|----------|---------|-----|--------|-------|
| 115200 | /4 | 108 | 115,740 | +0.47% |
| 9600 | /4 | 1302 | 9,596 | -0.04% |

**Fallback Mode (PCLK = 48 MHz)**:

| Baudrate | Divisor | BRR | Actual | Error |
|----------|---------|-----|--------|-------|
| 115200 | /4 | 103 | 116,279 | +1.0% |
| 9600 | /4 | 1249 | 9,615 | +0.16% |

Both modes produce acceptable error margins (<1% for 115200 baud). The fallback mode gracefully handles PLL/MOSC failure without requiring code changes.

---

## Pin Mapping (Board-Specific)

### CK-RA6M5

| Channel | TX | RX | PSEL | Location |
|---------|----|----|------|----------|
| UART3 | P707 | P706 | 0x05 | Arduino D1/D0 |

Used for debug console on CK-RA6M5 (set via `board_config.h: DEBUG_UART_CHANNEL = 3`).

### EK-RA6M5

| Channel | TX | RX | PSEL | Location |
|---------|----|----|------|----------|
| UART7 | P613 | P614 | 0x05 | Debug header |

Used for debug console on EK-RA6M5 (set via `board_config.h: DEBUG_UART_CHANNEL = 7`).

### All Channels Supported

Full table in `drv_uart.c: uart_pin_config()`:

| Channel | TX | RX | PSEL |
|---------|----|----|------|
| UART0 | P111 | P110 | 0x04 |
| UART1 | P709 | P708 | 0x05 |
| UART2 | P302 | P301 | 0x04 |
| UART3 | P707 | P706 | 0x05 (CK) / P310, P309 (alternate) |
| UART4 | P205 | P206 | 0x04 |
| UART5 | P501 | P502 | 0x05 |
| UART6 | P506 | P505 | 0x04 |
| UART7 | P613 | P614 | 0x05 (EK) |
| UART8 | PA00 | P607 | 0x04 |
| UART9 | P109 | P110 | 0x05 |

### Board Configuration

Pin selection automatically handled via `board_config.h`:

```c
/* board_config.h */
#if (BOARD_TYPE == BOARD_TYPE_CK)
  #define DEBUG_UART_CHANNEL  3   /* UART3 → P706/P707 */
  #define DEBUG_UART_BRR_DIV  4U  /* /4 divisor mode */
#else  /* BOARD_TYPE_EK */
  #define DEBUG_UART_CHANNEL  7   /* UART7 → P614/P613 */
  #define DEBUG_UART_BRR_DIV  4U  /* /4 divisor mode */
#endif
```

---

## Test Coverage

`src/test/test_uart.c` (hardware integration, requires powered target):
- `uart_scr_te_re_set`: SCR has TE+RE after UART_Init (board-agnostic)
- `uart_tdre_ready`: TDRE=1 after init
- `uart_brr_check`: BRR register matches expected value for configured board/divisor mode

Board-specific UART channel and pins are auto-tested based on `BOARD_TYPE` at compile time.

---

## Board-to-Board Porting

To test code on both boards without recompilation, edit `board_config.h` line 19:

```c
/* For CK-RA6M5 */
#define BOARD_TYPE      BOARD_TYPE_CK

/* For EK-RA6M5 */
#define BOARD_TYPE      BOARD_TYPE_EK
```

Then rebuild:
```bash
cmake --build build/
./build_and_flash.sh
```

All UART pins, clock divisor modes, and fallback behavior adapt automatically. Debug output shows:
```
BOARD : CK-RA6M5  (or EK-RA6M5)
UART  : SCI3 @ 115200 baud   (CK) or SCI7 @ 115200 baud (EK)
CLK   : ICLK=200000000 Hz, SCI_CLK=50000000 Hz
```
