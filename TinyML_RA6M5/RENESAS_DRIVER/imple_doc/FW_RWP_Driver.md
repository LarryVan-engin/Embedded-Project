# FW_RWP_Driver

Tags: #done #firmware #system

Register Write Protection driver. Files: `Driver/Include/drv_rwp.h`, `Driver/Source/drv_rwp.c`. Hardware register: [[HW_RA6M5_RWP]].

---

## API

```c
void RWP_Unlock_Clock_MSTP(void);
void RWP_Lock_Clock_MSTP(void);
```

---

## Implementation

```c
void RWP_Unlock_Clock_MSTP(void) {
    PRCR = (uint16_t)((0xA5U << 8U) | 0x03U);  /* unlock PRC0+PRC1 */
}
void RWP_Lock_Clock_MSTP(void) {
    PRCR = (uint16_t)(0xA5U << 8U);             /* lock: lower byte = 0 */
}
```

---

## Usage Pattern

All drivers that write SCKSCR, SCKDIVCR, or MSTPCR must bracket the write:

```c
RWP_Unlock_Clock_MSTP();
/* ... write protected register ... */
RWP_Lock_Clock_MSTP();
```

Callers: [[FW_Clock_Driver]] (CLK_Init, CLK_ModuleStart_SCI), [[FW_I2C_Driver]] (i2c_clock_init).

---

## Test Coverage

`src/test/test_rwp.c` verifies PRCR state directly on hardware:
- After unlock: lower byte bits [1:0] = 0x03
- After lock: lower byte = 0x00
