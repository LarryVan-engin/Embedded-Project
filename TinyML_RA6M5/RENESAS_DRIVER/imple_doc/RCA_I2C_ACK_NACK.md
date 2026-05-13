# RCA_I2C_ACK_NACK

Tags: #done #firmware #i2c

Root cause analysis for broken ACK/NACK logic in I2C master receive path. Affected module: [[FW_I2C_Driver]]. Hardware reference: [[HW_RA6M5_RIIC]].

---

## Symptoms

- I2C reads returned garbage data from slave
- Last byte of multi-byte read did not send NACK, so slave kept driving SDA
- Bus stuck busy after read sequence

---

## Hardware Root Cause

RA6M5 RIIC ICMR3 register controls the ACK/NACK bit (ACKBT) sent after each received byte.

**Hardware protection mechanism:** ACKBT is write-protected by ACKWP (bit 3 of ICMR3). Writing ACKBT when ACKWP=0 has **no effect** — the write is silently ignored. ACKWP must be set to 1 first, then ACKBT written, then ACKWP cleared.

**Last byte index:** The NACK must be sent with the **last byte** (index `length - 1`), not the second-to-last byte. Sending NACK on the penultimate byte terminates the read one byte early, losing the final byte.

---

## Root Cause in Code

Two major compounded bugs were found in Master Receive Mode:

**1. Missing Dummy Read & Late ACKBT Configuration:**
The original `I2C_Master_Receive_Data` waited for `RDRF` to become 1 before setting `ACKBT` and reading `ICDRR`.
However, in the RA RIIC, when a read address (`SLA+R`) is sent and acknowledged, the RIIC transfers the address byte to `ICDRR` and sets `RDRF=1`. The SCL line is held low until a **dummy read** of `ICDRR` is performed to start clocking the actual data bytes.
Because the original code skipped the dummy read, the first byte read was actually the dummy address byte (e.g., `0x71`). Furthermore, the hardware automatically started receiving the real first data byte with `ACKBT=0` (sending an ACK). For a 1-byte read (like `aht20_read_status`), sending an ACK caused the AHT20 sensor to pull SDA low for a second byte, which prevented the STOP condition from being generated and hung the bus.

**2. I2C_Start Recovery Return Bug:**
When the bus hung, the next call to `I2C_Start` timed out on the `BBSY` flag and triggered `i2c_bus_recover()`. However, immediately after recovery, the function executed a `return;` instead of proceeding to set `ICCR2_ST`. This meant no START condition was generated, causing the subsequent `I2C_Transmit_Address` to timeout and return `AHT20_ERR_NACK`.

**3. Incorrect STOP Condition Sequence in Master Receive:**
The original code read the last byte from `ICDRR` *before* setting the `SP` (STOP) bit. However, reading `ICDRR` automatically releases the SCL line and triggers the RIIC to clock the *next* byte. This caused the RIIC to clock a 7th byte after the AHT20 had already released SDA (sending `0xFF`). The STOP condition was delayed until after this 7th byte finished, leaving `RDRF=1` and `0xFF` in `ICDRR`. This "leftover byte" corrupted the *next* I2C transaction, leading to a shifted data frame where `data[0]` got the address byte (`0x71`) and everything else shifted, resulting in the impossible `T=149.9 C  RH=99.9%` calculation.

**4. AHT20 Clock Stretching Causing Timeout:**
The `DRV_TIMEOUT_TICKS` was originally set to `100,000`, which equates to ~12 ms at 8 MHz MOCO. The AHT20 sensor stretches the clock (holds SCL low) if a read is attempted while it is still processing the measurement (which takes ~80 ms). If the CPU delay loop `aht20_delay_ms(80)` finished slightly faster than 80 ms, the I2C driver initiated the read, but the AHT20 held SCL low for the remaining time. This caused the 12 ms timeout to expire, aborting the read (`AHT20 err=3`) and leaving the bus hung.

**5. Reversed ACKBT and ACKWP Bit Definitions:**
In `drv_i2c.h`, `ICMR3_ACKBT` was incorrectly defined as bit 4 and `ICMR3_ACKWP` as bit 3. The RA6M5 hardware actually maps `ACKWP` to bit 4 and `ACKBT` to bit 3. Because they were reversed, writing to `ACKBT` was always write-protected (ignored), and the driver *always* sent an ACK (0), never a NACK (1). Without a proper NACK on the final byte, the AHT20 failed to properly terminate its transmission sequence, leaving it in an unstable state that corrupted the *next* read cycle (resulting in `149.9 C`).

**6. I2C Clock Divider (CKS) Overflow:**
The baudrate calculation `br = (total/2)` yielded `br = 39` for 100 kHz at 8 MHz. However, the `ICBRL` and `ICBRH` registers only allocate 5 bits for the count (max 31), with the upper 3 bits fixed to `111`. When `39` (`0x27`) was written, the hardware truncated it to `7` (`0x07`). This caused the I2C bus to actually run at **~500 kHz** instead of 100 kHz! The AHT20 sensor (max 400 kHz) could not keep up with 500 kHz, leading to severe data corruption and bus hangs.

---

## Fixes Applied

1. **Master Receive STOP Sequence:** Updated `I2C_Master_Receive_Data` to set `ICCR2_SP = 1` *before* reading the last byte from `ICDRR`. This forces the RIIC to generate the STOP condition immediately after the read, preventing the extra 7th byte from being clocked.
2. **RDRF Flush:** Added a defensive `dummy = ICDRR` flush in `I2C_Start` if `RDRF=1` to guarantee a clean state before every transaction, preventing any leftover bytes from corrupting future reads.
3. **Timeout Value Increased:** Increased `DRV_TIMEOUT_TICKS` in `drv_common.h` from `100,000` to `1,000,000` (~125 ms). This allows the I2C driver to safely tolerate clock stretching by the AHT20 (which can take up to 80 ms) without prematurely timing out.
4. **Corrected ICMR3 Bit Definitions:** Swapped `ICMR3_ACKBT` (now bit 3) and `ICMR3_ACKWP` (now bit 4) in `drv_i2c.h`. The driver now correctly sends a NACK on the last byte, satisfying the I2C protocol and allowing the AHT20 to cleanly end its transmission.
5. **Dynamic Clock Divider (CKS):** Refactored `I2C_Init` in `drv_i2c.c` to dynamically calculate the `ICMR1.CKS` divider (Internal Reference Clock). If `br` exceeds 31, it automatically increases the clock divider (`/2`, `/4`, etc.) until `br <= 31`. This guarantees accurate 100 kHz and 400 kHz baudrates regardless of the base `PCLKB` frequency.
