# RCA_GPIO_Invalid_Port

Tags: #done #firmware #gpio

Root cause analysis for GPIO_PortToIndex returning 0 for invalid input. Affected module: [[FW_GPIO_Driver]]. Hardware reference: [[HW_RA6M5_GPIO]].

---

## Symptoms

- Calling GPIO functions with an invalid port character (e.g., 'Z') silently configured PORT0 registers
- No indication of invalid input; LED on PORT0 could be unintentionally driven
- Unit tests passed with wrong expected value

---

## Hardware Root Cause

RA6M5 has ports 0–9 and A–B. Port 0 (P000–P015) is a valid, real hardware port. Any index that aliases to PORT0 will cause real register writes to PORT0 hardware.

---

## Root Cause in Code

`GPIO_PortToIndex()` returned `0U` in the default case:

```c
uint8_t GPIO_PortToIndex(GPIO_PORT_t port) {
    switch (port) {
        case '0': return 0U;
        /* ... */
        default: return 0U;    /* WRONG: 0 is a valid port index (PORT0) */
    }
}
```

Callers did not check the return value. An invalid port passed to `GPIO_Config()` would silently configure PORT0.

---

## Fix

Add a sentinel constant and return it for invalid input:

```c
#define GPIO_INVALID_PORT  0xFFU

uint8_t GPIO_PortToIndex(GPIO_PORT_t port) {
    if (port >= '0' && port <= '9') return (uint8_t)(port - '0');
    if (port >= 'A' && port <= 'B') return (uint8_t)(port - 'A' + 10U);
    return GPIO_INVALID_PORT;   /* 0xFF — not a valid port index */
}
```

All three GPIO functions guard against the sentinel:

```c
uint8_t p = GPIO_PortToIndex(port);
if (p == GPIO_INVALID_PORT) { return; }
```

Unit test updated: `TEST_ASSERT_EQUAL(GPIO_INVALID_PORT, idx)` instead of `TEST_ASSERT_EQUAL(0U, idx)`.
