# FW_GPIO_Driver

Tags: #done #firmware #gpio

GPIO driver for RA6M5. Files: `Driver/Include/GPIO.h`, `Driver/Source/GPIO.c`. Hardware constraints: [[HW_RA6M5_GPIO]]. Bug fixes: [[RCA_GPIO_Invalid_Port]].

---

## API

```c
uint8_t GPIO_PortToIndex(GPIO_PORT_t port);
void    GPIO_Config(GPIO_PORT_t port, uint8_t pin, GPIO_CNF_t cnf, GPIO_MODE_t mode);
void    GPIO_Write_Pin(GPIO_PORT_t port, uint8_t pin, GPIO_PINSTATE_t state);
uint8_t GPIO_Read_Pin(GPIO_PORT_t port, uint8_t pin);
```

---

## GPIO_PortToIndex

Converts a `GPIO_PORT_t` character ('0'–'9', 'A', 'B') to a numeric index (0–11).

Returns `GPIO_INVALID_PORT (0xFF)` for any unrecognized input. All three GPIO functions (`GPIO_Config`, `GPIO_Write_Pin`, `GPIO_Read_Pin`) guard against this: if `p == GPIO_INVALID_PORT`, they return immediately.

Prior to the fix, invalid input returned `0`, which aliased PORT0. See [[RCA_GPIO_Invalid_Port]].

---

## GPIO_CNF_t Configuration Enum

| Value | Constant | PFS Configuration |
|-------|----------|-----------------|
| 0 | GPIO_CNF_IN_FLT | Input, floating (PMR=0, PDR=0) |
| 1 | GPIO_CNF_IN_PU | Input with pull-up (PCR=1) |
| 3 | GPIO_CNF_IN_ANA | Analog input (ASEL=1) |
| 5 | GPIO_CNF_OUT_PP | Push-pull output (PMR=0, PDR=1) |
| 6 | GPIO_CNF_OUT_OD | Open-drain output (NCODR=1, PDR=1) |
| 7 | GPIO_CNF_AF_PP | Peripheral function, push-pull (PMR=1) |
| 8 | GPIO_CNF_AF_OD | Peripheral function, open-drain (PMR=1, NCODR=1) |

---

## GPIO_MODE_t

RA6M5 does not have a drive-speed register. Valid values:

```c
GPIO_MODE_INPUT  = 0   /* PDR = 0 */
GPIO_MODE_OUTPUT = 1   /* PDR = 1 */
```

The original driver contained STM32-style `GPIO_MODE_OUT_10M`, `GPIO_MODE_OUT_2M`, `GPIO_MODE_OUT_50M`. These values were meaningless on RA6M5 and have been removed.

---

## PFS Write Sequence

All PFS writes require PWPR unlock/lock. See [[HW_RA6M5_GPIO]] for the sequence. `GPIO_Config` handles this internally.

---

## Test Coverage

`src/test/test_gpio.c`:
- `gpio_port_index_numeric`: indices 0–9 for '0'–'9'
- `gpio_port_index_alpha`: indices 10–11 for 'A'–'B'
- `gpio_port_index_invalid_sentinel`: returns `0xFF` for invalid input
