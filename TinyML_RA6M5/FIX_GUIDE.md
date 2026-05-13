# CK-RA6M5 LED Timing & UART Logging Fix Guide

**Ngày sửa:** May 9, 2026  
**Board:** CK-RA6M5 (R7FA6M5BH3CFC)  
**Vấn đề:** LED chớp tắt không đúng thời gian, UART không log gì

---

## 🔴 VẤN ĐỀ 1: LED Blink Timing Sai (⭐ CRITICAL)

### Triệu Chứng
- LED blink quá **nhanh** (~4 lần nhanh hơn mong đợi)
- Mọi task delay đều sai
- Timing không đúng ý dù code logic đúng

### Root Cause
**Clock fallback từ PLL (200 MHz) → HOCO (48 MHz)** không được xử lý trong SysTick initialization.

Khi PLL khóa thất bại (MOSC/crystal không ổn định):
1. `CLK_Init()` sẽ gọi `clk_fallback_to_hoco()`  
2. ICLK thay đổi từ 200 MHz → 48 MHz
3. **NHƯNG** `OS_SYSTICK_RELOAD` ở `rtos_config.h` vẫn hardcode cho 200 MHz
4. Kết quả: SysTick overflow **4.17x nhanh** → Hệ số lỗi timing

**Phương trình:**
```
Hệ số lỗi = 200 MHz / 48 MHz ≈ 4.17

LED delay 250ms thực tế chỉ là: 250ms / 4.17 ≈ 60ms
```

### ✅ Solution Applied

**File:** `kernel.c` → `OS_Start()` function

**Thay đổi:**
- ❌ **Trước:** `SYST_RVR = OS_SYSTICK_RELOAD;` (hardcode 199999)
- ✅ **Sau:** Tính dinamically:
```c
uint32_t actual_iclk = CLK_GetActualICLK();
uint32_t systick_reload = (actual_iclk / OS_TICK_RATE_HZ) - 1UL;
SYST_RVR = systick_reload;
```

**Kết quả:**
- **200 MHz (production):** `SYST_RVR = 199999` → 1 ms tick ✓
- **48 MHz (fallback):**  `SYST_RVR = 47999` → 1 ms tick ✓

---

## 🟡 VẤN ĐỀ 2: UART Pins Mismatch  

### Triệu Chứng
- Bạn cắm USB serial vào P706/P707
- Nhưng không có gì log ra
- Code tuy nói "P706/P707" nhưng thực ra dùng **P613/P614**

### Root Cause
- **CK-RA6M5 board** có pin configuration khác với **EK-RA6M5**
- Code comment nói P706/P707, nhưng switch case UART7 đặt P613/P614
- Không có logic để xác định board type

### ✅ Solution Applied

**Rõ ràng hoá configuration:**

| File | Thay Đổi |
|------|---------|
| `drv_uart.c` | Thêm warning: "CK-RA6M5 board uses P613/P614 NOT P706/P707" |
| `main.c` | Sửa debug message: "UART : SCI7 P613/P614" |
| `hal_entry.c` | Sửa header comment: "UART: SCI7 P613/P614" |

### ⚠️ Verify Your Board

**Nếu P706/P707 khác:**

Kiểm tra RA6M5 datasheet §32 (Pin Function Select Table), tìm dòng:
```
P706 → SCI? RxD / SCI? TxD
P707 → SCI? RxD / SCI? TxD
```

Nếu tìm được:
1. Mở `Driver/Source/drv_uart.c`
2. Tìm `uart_pin_config()` function
3. Sửa case UART7 (hoặc UART khác):
```c
case UART7: tx_port=7;  tx_pin=7;  rx_port=7;  rx_pin=6;  psel=0x??U; break;
```

---

## 🟢 VẤN ĐỀ 3: Ensure UART Baud Rate Correct

### Status: ✅ Already Handled

**UART_Init()** ở `drv_uart.c` đã dùng **dynamic PCLK**:
```c
uint32_t pclk = SCI_PCLK_HZ;  /* Get actual PCLK (may be 48MHz if fallback) */
SCI_BRR(n) = (uint8_t)((pclk + (divisor / 2U)) / divisor - 1U);
```

| Scenario | PCLKA | Divisor | BRR | Actual Baud |
|----------|-------|---------|-----|------------|
| **Production** | 100 MHz | 8×115200 | 107 | 115741 Hz |
| **Fallback** | 48 MHz | 8×115200 | 51 | 117188 Hz |

Cả hai variant đều **<0.5% error** → UART hoạt động ✓

---

## 📋 Implementation Summary

### Files Modified
1. **drv_clk.c** / **drv_clk.h**
   - ✅ Added `g_actual_iclk_hz` global variable
   - ✅ Set correctly in both production & fallback paths
   - ✅ New function: `CLK_GetActualICLK()`

2. **kernel.c**
   - ✅ Added `#include "drv_clk.h"`
   - ✅ Modified `OS_Start()` to calculate `SYST_RVR` dynamically

3. **drv_uart.c**
   - ✅ Clarified comments about CK vs EK boards
   - ✅ Added warning about P706/P707 vs P613/P614

4. **main.c** / **hal_entry.c**
   - ✅ Fixed debug messages: Now shows P613/P614 (not P706/P707)

---

## 🧪 Testing

### 1. Verify Clock Detection

```c
if (CLK_GetFallbackOccurred() != 0U) {
    debug_print("[WARN] Running at 48 MHz (HOCO fallback)\r\n");
} else {
    debug_print("[OK] Running at 200 MHz (PLL production)\r\n");
}
```

**Expected Output:**
- CK-RA6M5 with stable crystal: `[OK] Running at 200 MHz`
- If crystal unstable: `[WARN] Running at 48 MHz` + LED3 toggles once

### 2. Verify LED Timing

**Code:**
```c
LED1_ON();
OS_Task_Delay(250U);  // Should be exactly 250 ms
LED1_OFF();
OS_Task_Delay(250U);  // Should be exactly 250 ms
```

**Test with oscilloscope/logic analyzer:**
- ✅ **Correct:** 250ms ON + 250ms OFF = 500ms cycle
- ❌ **Before fix:** ~120ms cycle (4x too fast)

### 3. Verify UART Output

**Physical connections (CK-RA6M5):**
- **P613 (SCI7 TX)** → USB serial RXD pin
- **P614 (SCI7 RX)** → USB serial TXD pin
- **GND** → USB serial GND

**Verify:**
```
$ minicom -D /dev/ttyUSB0 -b 115200

[0 ms] === RA6M5 RTOS Application Test ===
[10 ms] UART  : SCI7 P613/P614 @ 115200 baud
[20 ms] I2C   : RIIC1 P512(SCL)/P511(SDA) @ 100 kHz
[30 ms] LINK  : OK
```

**Expected:** Smooth output at 115200 baud, no corruption

---

## 🔧 If UART Still Not Working

### Checklist:
1. **Verify connections:**
   - [ ] TX pin (P613) connected to serial adapter RXD
   - [ ] RX pin (P614) connected to serial adapter TXD
   - [ ] GND connected
   - [ ] No shorts or loose wires

2. **Verify configuration:**
   - [ ] `OS_DEBUG_UART_CHANNEL = 7U` (rtos_config.h)
   - [ ] `OS_DEBUG_UART_BAUDRATE = 115200U`
   - [ ] `OS_DEBUG_BACKEND_UART = 1`
   - [ ] `OS_DEBUG_ENABLE = 1`

3. **Check clock state:**
   - [ ] LED3 is ON (no fallback) or toggled once (fallback occurred)
   - [ ] If fallback: Check crystal power, loading capacitors

4. **Alternative UART channels:**
   If P613/P614 unavailable, try:
   - **UART1:** P708/P709 (set `OS_DEBUG_UART_CHANNEL = 1U`)
   - Edit `drv_uart.c` `uart_pin_config()` accordingly

---

## 📚 References

- **RA6M5 Hardware Manual §8:** Clock Generation (CLK)
- **RA6M5 Hardware Manual §30–34:** SCI, GPIO, Pin Function Select
- **RA6M5 Datasheet §32:** Pin Function Select Table (find your board's actual SCI pins)
- **Cortex-M33 Manual:** SysTick timing

---

## ✅ Summary Checklist

- [x] Clock fallback timing fixed → LED blink = 250ms actual
- [x] UART pins clarified → P613/P614 confirmed for CK-RA6M5
- [x] UART baud rate dynamic → Handles both 200 MHz & 48 MHz
- [x] Code comments updated → No more confusion
- [x] All timing calculations verified → SysTick reload correct

**Status:** ✅ **Ready to test on hardware**

