# RCA_UART_SSR_Manual_Clear

Tags: #done #firmware #uart

Root cause analysis for incorrect manual clearing of SSR.TDRE in the UART transmit path. Affected module: [[FW_UART_Driver]]. Hardware reference: [[HW_RA6M5_SCI]].

---

## Symptoms

- UART transmission appeared to work in some conditions but could silently drop bytes
- First byte after init sometimes corrupted
- Manual SSR write could inadvertently clear error flags (ORER, FER, PER)

---

## Hardware Root Cause

RA6M5 SCI SSR.TDRE (Transmit Data Register Empty, bit 7) is a **hardware-managed flag**:
- It is set by hardware when TDR becomes empty
- It is **automatically cleared by hardware when TDR is written**

There is no requirement to clear TDRE in software. The RA6M5 manual §34 explicitly states this.

Writing to SSR to clear TDRE is harmful because:
1. SSR also contains error flags (ORER, FER, PER) in the lower bits
2. A read-modify-write to clear TDRE can race with hardware setting an error flag, causing the error to be cleared before it is read

---

## Root Cause in Code

```c
/* WRONG: manual SSR clear in original SCI.c */
SCI_SSR(n) &= ~SSR_TDRE;    /* This clears TDRE manually — incorrect */
SCI_TDR(n) = (uint8_t)data;
```

The original code cleared TDRE before writing TDR. This is both unnecessary and dangerous.

---

## Fix

Remove the manual SSR clear entirely. Simply write TDR after polling TDRE:

```c
/* CORRECT */
while (!(SCI_SSR(n) & SSR_TDRE)) {}   /* wait for hardware to set TDRE */
SCI_TDR(n) = (uint8_t)data;            /* hardware clears TDRE on write */
```

Hardware clears TDRE on the TDR write. No software action on SSR is required or correct.
