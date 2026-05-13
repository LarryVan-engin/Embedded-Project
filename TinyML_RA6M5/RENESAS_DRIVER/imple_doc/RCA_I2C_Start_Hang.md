# RCA_I2C_Start_Hang

Tags: #done #firmware #i2c

Root cause analysis for I2C_Start infinite hang after issuing a START condition. Affected module: [[FW_I2C_Driver]]. Hardware reference: [[HW_RA6M5_RIIC]].

---

## Symptoms

- System hangs permanently inside `I2C_Start()` on the first I2C transaction
- Debugger shows execution stuck in a `while` loop polling ICSR2
- LED never blinks; watchdog resets (if enabled)

---

## Hardware Root Cause

RA6M5 RIIC has two separate "transfer complete" signals:

| Flag | Register.Bit | Meaning |
|------|-------------|---------|
| TEND | ICSR2.6 | Set when a **data byte** transmission completes |
| BBSY | ICCR2.7 | Set when the bus transitions from idle to busy (START issued) |

TEND is only valid after a full byte (address or data) has been clocked out. It is **not set after a START condition** — START generates no data transfer, so TEND never fires.

---

## Root Cause in Code

Original `I2C_Start()` polled TEND immediately after issuing START:

```c
ICCR2(n) |= ICCR2_ST;               /* request START */
while (!(ICSR2(n) & ICSR2_TEND)) {} /* WRONG: waits for data byte end */
                                     /* TEND is never set after START → infinite hang */
```

---

## Fix

Remove the TEND poll entirely. After START, poll `BBSY` to confirm the bus has become busy:

```c
while (ICCR2(n) & ICCR2_BBSY) {}    /* wait: bus must be free first */
ICCR2(n) |= ICCR2_ST;               /* request START condition       */
while (!(ICCR2(n) & ICCR2_BBSY)) {} /* wait: bus busy = START sent   */
```

Both loops are now protected by `DRV_TIMEOUT_TICKS`. See `drv_common.h`.
