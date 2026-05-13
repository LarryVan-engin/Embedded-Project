# ZMOD4410 Gas Sensor Module Integration

## Overview

The **ZMOD4410** is a Renesas gas sensor for indoor-air applications. In Renesas' reference software, the device is **not** treated as a simple register-mapped IAQ sensor that directly returns IAQ/TVOC/eCO2 values. The official flow is:

1. Verify the product ID over I2C.
2. Load a mode-specific configuration data set into sequencer registers.
3. Start a measurement.
4. Read raw ADC/result bytes.
5. Run a host-side Renesas algorithm to compute IAQ, TVOC, eCO2, PBAQ, or Relative IAQ.

The current BSP in this repository intentionally implements only the **verified low-level transport layer**:

- I2C address `0x32`
- PID check
- measurement trigger
- busy/status polling
- raw ADC/result readback
- ambient temperature/humidity caching for future algorithm use

High-level IAQ/TVOC/eCO2 calculation is **not** implemented yet.

## Verified Register Model

The current BSP and this note are aligned with the published Renesas ZMOD4xxx/FSP model for ZMOD4410:

| Item | Value | Notes |
|------|-------|-------|
| I2C address | `0x32` | 7-bit address |
| PID register | `0x00` | 2-byte product ID |
| Expected PID | `0x2310` | ZMOD4410 product ID |
| Command register | `0x93` | Write `0x80` to start a measurement |
| Status register | `0x94` | Bit 7 indicates sequencer busy/running |
| Result register | `0x97` | Raw ADC/result bytes |
| Configuration blocks | `0x40/0x50/0x60/0x68` | Mode-specific H/D/M/S data |
| Production data | `0x26` | Required by full Renesas flow |

## Operation Modes

The ZMOD4410 supports several Renesas-defined operating modes. Each one requires its own configuration data set and host-side algorithm handling.

| Mode | Typical sample time | Typical warm-up | Intended result |
|------|---------------------|-----------------|-----------------|
| IAQ 2nd Gen | 3 s | 100 samples | Absolute IAQ / TVOC / eCO2 |
| IAQ 2nd Gen ULP | 90 s | 10 samples | Ultra-low-power IAQ |
| Public Building Air Quality | 5 s | 60 samples | PBAQ output |
| Relative IAQ | 3 s | 100 samples | Relative air-quality trend |
| Relative IAQ ULP | 90 s | 10 samples | Ultra-low-power relative IAQ |

## Current BSP Scope

### Implemented

- `ZMOD4410_Init()` reads PID `0x2310` and stores the requested mode.
- `ZMOD4410_Trigger_Measurement()` writes the start command to register `0x93`.
- `ZMOD4410_Measurement_Ready()` polls register `0x94` and checks the busy bit.
- `ZMOD4410_Read()` reads raw result bytes from register `0x97` and copies them into `ZMOD4410_Data_t.raw_adc`.
- `ZMOD4410_Set_Ambient_Conditions()` stores temperature and humidity locally.

### Not Yet Implemented

- Writing Renesas H/D/M/S configuration blocks for each mode
- Reading production data from `0x26`
- Running the Renesas host-side gas algorithm
- Returning computed IAQ/TVOC/eCO2/PBAQ/Relative-IAQ values
- Device-error register handling and full diagnostic flow
- INT-pin driven measurement completion support

Because of those missing layers, `iaq_index`, `tvoc_ppb`, `tvoc_mg_m3`, and `eco2_ppm` are currently left at zero and should not be treated as valid outputs.

## Hardware Integration

### Pin Configuration

```
Pin #1 (SCL)   → P501 (I2C0 clock)     ← with 4.7 kΩ pull-up to 3.3 V
Pin #2 (SDA)   → P500 (I2C0 data)      ← with 4.7 kΩ pull-up to 3.3 V
Pin #3 (INT)   → P511 (optional)       ← interrupt signal (HIGH=measuring, LOW=complete)
Pin #5 (VDD)   → 3.3 V
Pin #6 (VSS)   → GND
Pin #10 (VDDH) → 3.3 V (heater supply) ← recommended
Pin #11 (RES_N)→ 3.3 V (optional)      ← active-LOW reset
Pin #12 (VDDIO)→ 3.3 V (I/O supply)

Blocking capacitors:
  - 100 nF ceramic capacitor between each supply pin and GND
  - Place close to sensor module
```

### Connection Example (EK-RA6M5)

```
ZMOD4410      EK-RA6M5
────────────  ────────────
SCL (#1)   ── P501 (J8-1)  ← 4.7k Ω ↑ 3.3V
SDA (#2)   ── P500 (J8-2)  ← 4.7k Ω ↑ 3.3V
VDD (#5)   ── 3.3 V
VDDH (#10) ── 3.3 V
VDDIO(#12) ── 3.3 V
VSS (#6)   ── GND
INT (#3)   ── P511 (optional, for interrupt-driven mode)
RES_N (#11)── 3.3 V (optional, pulled HIGH = normal operation)
```

## Software Integration

### BSP Driver Files

- **Header**: `BSP/ZMOD4410/bsp_zmod4410.h`
- **Source**: `BSP/ZMOD4410/bsp_zmod4410.c`

### API Usage Example

```c
#include "bsp_zmod4410.h"

// Initialize I2C (must be called first)
I2C_Init(I2C0, 8U, I2C_SPEED_FAST);  // 8 MHz PCLKB, 400 kHz

// Initialize sensor for IAQ 2nd Generation mode
ZMOD4410_Status_t status = ZMOD4410_Init(I2C0, IAQ_2ND_GEN);
if (status != ZMOD4410_OK) {
    debug_print("ZMOD4410 init failed: %d\r\n", status);
    return;
}

// Main measurement loop (3 seconds shown for IAQ 2nd Gen)
while (1) {
    ZMOD4410_Trigger_Measurement(I2C0);

    OS_Task_Delay(3000);  // 3 seconds for IAQ 2nd Gen

    ZMOD4410_Data_t data;
    if (ZMOD4410_Read(I2C0, &data) == ZMOD4410_OK) {
    debug_print("status=%u raw_len=%u raw0=0x%02X raw1=0x%02X\r\n",
          (unsigned)data.status,
          (unsigned)data.raw_len,
          data.raw_adc[0],
          data.raw_adc[1]);
    }
}
```

## Manual AI Model Test with Real ZMOD4410

To support AI testing from real sensor data without changing normal boot flow, a manual-only test module is available:

- `src/test/test_iaq_zmod4410_real.h`
- `src/test/test_iaq_zmod4410_real.c`

Entry point:

```c
void test_iaq_zmod4410_real_manual_run(I2C_t i2c, uint8_t pclkb_mhz, uint32_t sample_count);
```

Behavior:

1. Initializes I2C and ZMOD4410 in IAQ 2nd Gen mode.
2. Initializes the IAQ AI model (`IAQ_Init`).
3. Triggers measurement, waits 3 seconds, reads raw bytes.
4. Builds a temporary `tvoc_proxy` from `raw_adc[0..1]` and runs `IAQ_Predict`.
5. Prints raw bytes and predicted IAQ for each sample.

Important scope note:

- This test is intentionally **not** registered in `test_cases.h`.
- This test is intentionally **not** called from `hal_entry.c` or `main.c`.
- It will not run unless manually invoked by developer code during debug.

### Polling vs. Interrupt-Driven Operation

#### Polling Mode (Recommended for Simplicity)
```c
ZMOD4410_Trigger_Measurement(i2c);
OS_Task_Delay(3000);  // Wait full sample time
ZMOD4410_Data_t data;
ZMOD4410_Read(i2c, &data);
```

#### Interrupt-Driven Mode (Advanced)
```c
// Configure INT pin as external interrupt (falling edge)
// In interrupt handler:
if (ZMOD4410_Measurement_Ready(i2c)) {
    ZMOD4410_Data_t data;
  ZMOD4410_Read(i2c, &data);  // Consume raw bytes
    ZMOD4410_Trigger_Measurement(i2c);  // Start next measurement
}
```

### Integration with Other Drivers

**AHT20 Humidity/Temperature Compensation:**
```c
AHT20_Data_t aht_data;
if (AHT20_Read(I2C0, &aht_data) == AHT20_OK) {
  // Cache ambient conditions for future algorithm compensation
    ZMOD4410_Set_Ambient_Conditions(I2C0, 
                                     aht_data.temperature_c,
                                     aht_data.humidity_pct);
}
```

## Operation Characteristics

### Warm-up Behavior

- **Initial warm-up**: discard the first measurements according to the chosen mode
- **Current BSP behavior**: tracks warm-up locally by sample count, not by a dedicated warm-up status bit
- **Baseline learning**: the official Renesas algorithm continues to adapt over time
- **Conditioning**: continuous operation before deployment is still recommended for stable IAQ behavior

### Sample Time Requirements

Before reading, must wait at least:
- **3 seconds** for IAQ 2nd Gen and Relative IAQ
- **5 seconds** for PBAQ
- **90 seconds** for ULP modes

### Environmental Influences

- **Temperature Range**: 
  - Full operation: –40 °C to +65 °C
  - Best accuracy: 0 °C to +40 °C
  - Recommended: 21 °C for PBAQ
  
- **Humidity Range**:
  - Full operation: 0–90% RH (non-condensing)
  - Recommended for PBAQ: 50% RH
  
- **Temperature/Humidity Compensation**: Optional input today, required for full Renesas algorithm quality

## Measurement Flow in This Repository

1. `ZMOD4410_Init()` checks the sensor PID at register `0x00`.
2. `ZMOD4410_Trigger_Measurement()` writes `0x80` to command register `0x93`.
3. `ZMOD4410_Measurement_Ready()` polls status register `0x94` until the busy bit clears.
4. `ZMOD4410_Read()` reads raw result bytes from register `0x97`.
5. Application code may log or inspect `raw_adc[]` now; final IAQ conversion remains a future integration task.

### Sensor Limitations

⚠️ **Safety Disclaimer** (from datasheet):
> The ZMOD4410 can detect safety-relevant gases such as CO (carbon monoxide), but is **NOT** designed to detect these gases reliably. It is **NOT approved for safety-critical or life-protecting applications**. Do not use in such applications. Renesas disclaims all liability for any such use.

## Electrical Specifications

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Supply Voltage (VDD) | 1.7 | – | 3.6 | V |
| Active Mode Current | – | 5.2 | 10.3 | mA @ 3.3V |
| Sleep Mode Current | – | 450 | – | nA |
| I2C Clock Frequency | – | – | 400 | kHz |
| Start-up Time (to communication) | – | – | 1 | ms |
| Lifetime (JEDEC qualified) | 10 | – | – | years |

## Troubleshooting

| Symptom | Possible Cause | Solution |
|---------|-----------------|----------|
| I2C NACK / No response | Wiring issue, wrong address, power off | Check SCL/SDA pull-ups, verify address `0x32`, check power |
| PID mismatch | Wrong device on bus or read failure | Confirm ZMOD4410 is populated and responding at `0x00` |
| Busy never clears | Measurement not started correctly or config/state issue | Verify command write to `0x93`, power rails, and sensor reset behavior |
| High-level IAQ fields stay zero | Expected with current BSP scope | Use `raw_adc[]` for now; add official Renesas algorithm layer later |

## References

- **ZMOD4410 Datasheet**: R36DS0027EU0114 Rev. 1.14 (Mar 10, 2023)
- **Renesas FSP ZMOD4xxx stack**: reference register map, mode data sets, and host-side algorithms
- **UBA Standards**: Umweltbundesamt guidelines for indoor air quality

## Implementation Notes

### Current Status

- ✅ Verified low-level register map for PID / CMD / STATUS / RESULT
- ✅ Raw measurement trigger and readback path
- ✅ Ambient-condition caching API
- ⏳ Renesas mode data-set loading
- ⏳ Production-data handling
- ⏳ Host-side IAQ/TVOC/eCO2/PBAQ/Relative-IAQ algorithms

### Future Enhancements

1. **Mode Data-Set Integration**: Add Renesas H/D/M/S configuration data for the supported modes.
2. **Production Data Readout**: Pull the required factory data from register `0x26`.
3. **Algorithm Integration**: Add the official Renesas host-side calculations for IAQ and related outputs.
4. **Device Diagnostics**: Add device-error checks and more explicit fault reporting.
5. **Interrupt-driven Measurements**: Reduce polling overhead via the INT pin when the hardware wiring is available.
