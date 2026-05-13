# RCA_I2C_Init_Sequence

Tags: #done #firmware #i2c

Root cause analysis for I2C initialization sequence violation. Affected module: [[FW_I2C_Driver]]. Hardware constraint source: [[HW_RA6M5_RIIC]].

---

## Symptoms

- I2C peripheral failed to initialize correctly
- SCL/SDA clock generation incorrect or absent after init
- Intermittent bus lockup on first transaction after power-on

---

## Hardware Root Cause

RA6M5 RIIC hardware manual §38.3 specifies a mandatory initialization order. The peripheral contains an internal state machine that must be reset *before* being enabled. The required sequence is:

```
ICE=0, IICRST=0  →  IICRST=1  →  ICE=1  →  configure  →  IICRST=0
```

The reset (IICRST) must be asserted while the peripheral is **disabled** (ICE=0). Only after IICRST=1 is it safe to set ICE=1. Reversing this order leaves the RIIC internal state machine in an undefined state when the reset is released.

---

## Root Cause in Code

Original `IIC.c` set ICE before IICRST:

```c
/* WRONG: original sequence */
ICCR1(n) |= ICCR1_ICE;      /* ICE=1 first — violates §38.3 */
ICCR1(n) |= ICCR1_IICRST;   /* IICRST=1 after — too late    */
```

Additionally, the original code did not write ICBRL/ICBRH with the mandatory 0xE0 upper bits, violating the hardware requirement in §38.2.11.

---

## Fix

```c
ICCR1(n) = 0x00U;           /* Step 1: full disable           */
ICCR1(n) |= ICCR1_IICRST;  /* Step 2: assert reset (ICE=0)   */
ICCR1(n) |= ICCR1_ICE;     /* Step 3: enable (while in reset) */
ICBRL(n) = 0xE0U | br;     /* Step 4: bit rate (0xE0 required)*/
ICBRH(n) = 0xE0U | br;
/* ... ICMR config ... */
ICCR1(n) &= ~ICCR1_IICRST; /* Step 5: release reset           */
```
