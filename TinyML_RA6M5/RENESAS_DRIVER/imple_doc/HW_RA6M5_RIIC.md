# HW_RA6M5_RIIC

Tags: #done #hardware #i2c

RIIC (I2C Bus Interface) peripheral on RA6M5. Supports standard (100 kHz) and fast (400 kHz) master mode. Firmware driver: [[FW_I2C_Driver]]. Bug history: [[RCA_I2C_Init_Sequence]], [[RCA_I2C_Start_Hang]], [[RCA_I2C_ACK_NACK]].

---

## Peripheral Map

Base address: `0x4009F000`. Stride per channel: `0x100`. Three channels: I2C0–I2C2.

Module stop release required before use. See [[FW_Clock_Driver]].

MSTPCRB bit mapping: I2C0=bit9, I2C1=bit8, I2C2=bit7.

---

## Register Map (per channel, 8-bit registers)

| Offset | Register | Function |
|--------|----------|----------|
| 0x00 | ICCR1 | Bus Control 1 (ICE, IICRST) |
| 0x01 | ICCR2 | Bus Control 2 (ST, SP, RS, BBSY) |
| 0x02 | ICMR1 | Mode Register 1 |
| 0x03 | ICMR2 | Mode Register 2 |
| 0x04 | ICMR3 | Mode Register 3 (ACKBT, ACKWP) |
| 0x05 | ICFER | Function Enable Register |
| 0x09 | ICSR2 | Status Register 2 (TDRE, TEND, RDRF, NACKF, STOP) |
| 0x10 | ICBRL | Bit Rate Low |
| 0x11 | ICBRH | Bit Rate High |
| 0x12 | ICDRT | Transmit Data Register |
| 0x13 | ICDRR | Receive Data Register |

---

## Critical Hardware Constraints

### ICCR1: Initialization Sequence (§38.3)

The mandated sequence is:
1. `ICCR1 = 0x00` — ICE=0, IICRST=0 (full disable)
2. `ICCR1 |= IICRST` — assert reset **while ICE=0**
3. `ICCR1 |= ICE` — enable **while still in reset**
4. Configure ICBRL, ICBRH, ICMR registers
5. `ICCR1 &= ~IICRST` — release reset

**Setting ICE before IICRST violates this sequence** — see [[RCA_I2C_Init_Sequence]].

### ICBRL / ICBRH: Fixed Upper Bits

Bits [7:5] of ICBRL and ICBRH **must always be written as 1** (hardware requirement, §38.2.11).

```c
ICBRL(n) = 0xE0U | br_value;   /* 0xE0 = ICBR_FIXED_BITS */
```

### ICMR3: ACKWP Must Precede ACKBT

To send NACK on last received byte, the sequence must be:
1. Set `ACKWP=1` (write-protect unlock)
2. Set `ACKBT=1` (NACK) or clear `ACKBT=0` (ACK)
3. Clear `ACKWP=0` (re-lock)

Writing ACKBT without ACKWP=1 has no effect. See [[RCA_I2C_ACK_NACK]].

---

## BRR Formula

```
total_counts = (PCLKB_hz / speed_hz) - 2
br = total_counts / 2
ICBRL = ICBRH = 0xE0 | br
```

| PCLKB | Speed | br | ICBRL/ICBRH | Actual SCL |
|-------|-------|----|-------------|------------|
| 8 MHz | 100 kHz | 39 | 0xE7 | 100 kHz |
| 8 MHz | 400 kHz | 9 | 0xE9 | 400 kHz |

PCLKB source: [[HW_RA6M5_ClockTree]].

---

## Bus Arbitration Signals

| Register | Bit | Name | Meaning |
|----------|-----|------|---------|
| ICCR2 | 7 | BBSY | Bus busy. Poll == 0 before START |
| ICCR2 | 3 | SP | STOP condition request |
| ICCR2 | 1 | ST | START condition request |
| ICSR2 | 6 | TEND | Transmit complete (byte sent) |
| ICSR2 | 5 | RDRF | Receive data full (byte received) |
| ICSR2 | 3 | STOP | STOP condition detected |

**Do not poll TEND after sending a READ address (SLA+R)**. The RIIC automatically transitions to receive mode upon an address match, setting `RDRF=1` (or `NACKF=1` on failure), not `TEND`. Polling `TEND` for a read address will result in an infinite loop/timeout. See [[RCA_I2C_Start_Hang]].

---

## Pin Configuration

I2C pins require open-drain mode (NCODR=1). The RA6M5 internal pull-up resistor (~50 kΩ, PFS PCR bit4=1) is compatible with PMR=1+NCODR=1 mode and is **enabled by the driver** for all I2C channels. This is sufficient for short PCB traces (< 10 cm) at 100 kHz. For longer traces or 400 kHz, add external 4.7 kΩ resistors to 3.3 V. See [[HW_RA6M5_GPIO]].

| Channel | SCL | SDA |
|---------|-----|-----|
| I2C0 | P400 | P401 |
| I2C1 | P512 | P511 |
| I2C2 | P410 | P409 |

PSEL = 0x07 for RIIC function.

---

## References

RA6M5 Hardware Manual §38 (RIIC).
