# BSP_AHT20

Tags: #done #bsp #i2c #sensor

BSP driver for ASAIR AHT20 temperature + humidity sensor over I2C.
Files: `BSP/AHT20/bsp_aht20.h`, `BSP/AHT20/bsp_aht20.c`.
Hardware reference: [[HW_RA6M5_RIIC]], [[FW_I2C_Driver]].

---

## Hardware

| Property | Value |
|----------|-------|
| Sensor | ASAIR AHT20 |
| Interface | I2C, 7-bit address `0x38` |
| Supply | 2.0–5.5 V (board: 3.3 V) |
| Range | −40 to +85 °C, 0–100 %RH |
| Accuracy | ±0.3 °C, ±2 %RH |

**Board connections (EK-RA6M5, RIIC1 — J24 connector):**

| Signal | MCU Pin | J24 Pin |
|--------|---------|---------|
| SCL | P512 | J24-10 |
| SDA | P511 | J24-9 |
| VCC | 3.3 V | any 3.3 V |
| GND | GND | any GND |

External pull-up resistors **4.7 kΩ to 3.3 V** are recommended for wire lengths > 10 cm. For short PCB traces, the RA6M5 internal pull-up (~50 kΩ, enabled by `i2c_pin_config`) is sufficient at 100 kHz.

---

## API

```c
#include "bsp_aht20.h"

typedef enum {
    AHT20_OK          = 0,
    AHT20_ERR_NACK    = 1,   /* no ACK — check wiring */
    AHT20_ERR_BUSY    = 2,   /* still measuring — retry */
    AHT20_ERR_TIMEOUT = 3    /* I2C bus timeout */
} AHT20_Status_t;

typedef struct {
    float temperature_c;   /* °C  (-40 to +85)  */
    float humidity_pct;    /* %RH (0 to 100)     */
} AHT20_Data_t;

void           AHT20_Init(I2C_t i2c);
AHT20_Status_t AHT20_Read(I2C_t i2c, AHT20_Data_t *out);
```

---

## Usage

```c
/* One-time init (call after I2C_Init, ≥100 ms after power-on) */
I2C_Init(I2C1, 50U, I2C_SPEED_STANDARD);
AHT20_Init(I2C1);

/* Read in main loop */
AHT20_Data_t data;
if (AHT20_Read(I2C1, &data) == AHT20_OK) {
    debug_print("T=%.1f  RH=%.1f%%\r\n",
                (double)data.temperature_c,
                (double)data.humidity_pct);
}
```

---

## Protocol Summary (AHT20 datasheet Rev 1.1)

### Initialisation
1. Power-on → wait ≥ 100 ms
2. Read status byte (`0x71`): check bit 3 (calibration flag)
3. If bit 3 = 0: send init command `0xBE 0x08 0x00`, wait 10 ms

### Measurement
1. Send trigger: `0xAC 0x33 0x00`
2. Wait ≥ 80 ms
3. Read 6 bytes

Current implementation detail:
- `AHT20_Init()` still uses a pre-RTOS busy-wait because it is called before `OS_Start()`.
- `AHT20_Read()` uses `OS_Task_Delay()` once the scheduler is running, so the 80 ms conversion wait tracks the kernel tick instead of CPU-bound loop calibration.

### Data Frame (6 bytes)

| Byte | Content |
|------|---------|
| 0 | Status (bit7=busy, bit3=calibrated) |
| 1 | RH[19:12] |
| 2 | RH[11:4] |
| 3 | RH[3:0] ‖ T[19:16] |
| 4 | T[15:8] |
| 5 | T[7:0] |

### Conversion

```
raw_rh = (buf[1]<<12) | (buf[2]<<4) | (buf[3]>>4)
raw_t  = ((buf[3]&0x0F)<<16) | (buf[4]<<8) | buf[5]

RH (%)  = raw_rh / 2^20 × 100
T  (°C) = raw_t  / 2^20 × 200 − 50
```

---

## Build Integration

CMake picks up BSP sources automatically:

```cmake
# cmake/GeneratedSrc.cmake
file(GLOB_RECURSE Source_Files
    ...
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/**/*.c
)
target_include_directories(... ${CMAKE_CURRENT_SOURCE_DIR}/BSP/AHT20 ...)
```

Include in code: `#include "bsp_aht20.h"`
