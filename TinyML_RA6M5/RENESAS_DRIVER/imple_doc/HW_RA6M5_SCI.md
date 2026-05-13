# HW_RA6M5_SCI

Tags: #done #hardware #uart

SCI (Serial Communication Interface) peripheral on RA6M5. Used in asynchronous UART mode. Firmware driver: [[FW_UART_Driver]]. Bug history: [[RCA_UART_BRR_SEMR]], [[RCA_UART_SSR_Manual_Clear]].

---

## Peripheral Map

Base address: `0x40118000`. Stride per channel: `0x100`.

10 channels: SCI0–SCI9. Register address: `SCI_BASE + 0x100 * n + offset`.

Module stop release required before use. See [[FW_Clock_Driver]] (CLK_ModuleStart_SCI).

---

## Register Map (per channel, 8-bit registers)

| Offset | Register | Function |
|--------|----------|----------|
| 0x00 | SMR | Serial Mode Register |
| 0x01 | BRR | Bit Rate Register |
| 0x02 | SCR | Serial Control Register |
| 0x03 | TDR | Transmit Data Register |
| 0x04 | SSR | Serial Status Register |
| 0x05 | RDR | Receive Data Register |
| 0x07 | SEMR | Serial Extended Mode Register |

---

## SCR Bits

| Bit | Name | Function |
|-----|------|----------|
| 7 | TIE | Transmit interrupt enable |
| 6 | RIE | Receive interrupt enable |
| 5 | TE | Transmit enable |
| 4 | RE | Receive enable |

Set SCR=0x00 before configuring. Re-enable with `SCR_TE | SCR_RE` after BRR is written.

---

## SSR Bits

| Bit | Name | Function |
|-----|------|----------|
| 7 | TDRE | Transmit Data Register Empty — poll before writing TDR |
| 6 | RDRF | Receive Data Register Full — poll before reading RDR |
| 5 | ORER | Overrun error |
| 4 | FER | Framing error |

**TDRE is cleared automatically by hardware when TDR is written. Never clear it manually** — see [[RCA_UART_SSR_Manual_Clear]].

---

## SEMR Bits (§34 Table 34.12)

| Bit | Name | Function |
|-----|------|----------|
| 6 | BGDM | Baud Rate Generator Double-Speed Mode |
| 4 | ABCS | Asynchronous Base Clock Select (1 = 8 clocks/bit) |

On RA6M5, SCI uses `PCLKA` as its internal baud-generation clock.

With BGDM=1 and ABCS=1: divisor coefficient = 8.

**These two bits were swapped in the original driver** — see [[RCA_UART_BRR_SEMR]].

---

## BRR Formula

With SEMR: BGDM=1 (bit 6), ABCS=1 (bit 4):
```
BRR = PCLKA / (8 × baudrate) - 1
```

Example at PCLKA = 100 MHz, 115200 baud:
```
BRR = 100 000 000 / (8 × 115 200) - 1 = 107  (with rounding)
Actual baud = 100 000 000 / (8 × 108) = 115 741 Hz  (+0.47% error — within 5% spec)
```

PCLKA source: [[HW_RA6M5_ClockTree]].

---

## Pin Function Select (UART mode)

Set `PmnPFS.PSEL` to the SCI function value for each channel. Set `PMR=1` to switch pin to peripheral mode. Requires PWPR unlock — see [[HW_RA6M5_GPIO]].

---

## References

RA6M5 Hardware Manual §34 (SCI).
