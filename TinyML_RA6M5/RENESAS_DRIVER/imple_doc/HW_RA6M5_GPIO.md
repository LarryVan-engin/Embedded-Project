# HW_RA6M5_GPIO

Tags: #done #hardware #gpio

GPIO and Pin Function Select (PFS) system on RA6M5. Controls digital I/O, direction, open-drain mode, pull-up, and peripheral mux (PMR). Firmware driver: [[FW_GPIO_Driver]]. Bug history: [[RCA_GPIO_Invalid_Port]].

---

## Port Control Registers (PCNTR)

Base: `0x40080000`. Stride per port: `0x20`.

| Offset | Register | Function |
|--------|----------|----------|
| 0x000 | PCNTR1 | Output data + direction (combined 32-bit) |
| 0x002 | PDR | Port Direction Register — 1=output, 0=input (16-bit) |
| 0x004 | PCNTR2 | Input data (PIDR) + output data (PODR) |
| 0x008 | PCNTR3 | Set/clear output via POSR/PORR bit-fields |

---

## Pin Function Select (PFS)

Base: `0x40080800`. Address per pin: `PFS_BASE + 0x40 * port + 0x04 * pin`.

### Key PmnPFS Bit Fields

| Bit | Name | Function |
|-----|------|----------|
| 16 | PMR | 0=GPIO mode, 1=Peripheral function mode |
| 28:24 | PSEL | Peripheral function selector |
| 6 | NCODR | Open-drain enable (required for I2C) |
| 4 | PCR | Pull-up resistor enable |

### PWPR — PFS Write-Protection Register (PFS_BASE + 0x503)

PFS registers are write-protected by PWPR. Unlock sequence:

```c
PWPR = 0x00U;   /* Step 1: clear B0WI  (allows writing PFSWE) */
PWPR = 0x40U;   /* Step 2: set  PFSWE  (allows writing PFS)   */
/* ... write PmnPFS ... */
PWPR = 0x00U;   /* Step 3: clear PFSWE */
PWPR = 0x80U;   /* Step 4: set  B0WI   (locks PFS writes)     */
```

---

## Port Enumeration

Ports 0–9 map to indices 0–9. Ports A–B map to indices 10–11.

`GPIO_PortToIndex()` converts a `GPIO_PORT_t` character value to a numeric index. Returns `GPIO_INVALID_PORT (0xFF)` for invalid input — not 0, which would alias PORT0. See [[RCA_GPIO_Invalid_Port]].

---

## GPIO Modes on RA6M5

RA6M5 has no drive-speed register (unlike STM32). Only two meaningful modes:

| Mode | Value | Meaning |
|------|-------|---------|
| GPIO_MODE_INPUT | 0 | PDR bit = 0 |
| GPIO_MODE_OUTPUT | 1 | PDR bit = 1 |

STM32-style speed values (10M/2M/50M) are meaningless on this MCU. See [[FW_GPIO_Driver]].

---

## References

RA6M5 Hardware Manual §19 (I/O Ports), §20 (Pin Function Select).
