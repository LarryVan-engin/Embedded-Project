---
type: hub
status: in-progress
last_updated: 2025-07-11
---

# RENESAS_DRIVER — Knowledge Base

Tags: #in-progress #system

> Project: TESTING_2 | Target: R7FA6M5BH3CFC (Cortex-M33) | Board: EK-RA6M5
> Build: CMake + Ninja, ARM GCC, `file(GLOB_RECURSE)` auto-collects `Driver/Source/*.c`, `src/*.c`, `BSP/**/*.c`, `Middleware/Kernel/**/*.c`, `Middleware/Kernel/**/*.S`

---

## Hardware Layer

| Note | Covers |
|------|--------|
| [[HW_RA6M5_ClockTree]] | XTAL/HOCO/MOCO/PLL, SCKDIVCR, SCKSCR, PCLKA/PCLKB |
| [[HW_RA6M5_SCI]] | SCI UART registers, SEMR, BRR formula, SSR flags |
| [[HW_RA6M5_RIIC]] | RIIC I2C registers, init sequence, ICBRL fixed bits |
| [[HW_RA6M5_GPIO]] | Port control, PFS, PWPR, PmnPFS_PSEL bits[28:24] |
| [[HW_RA6M5_RWP]] | PRCR register, write key 0xA5, PRC0/PRC1 |
| [[HW_RA6M5_USBFS]] | USBFS host/device role, EP0, FIFO, PIPE, interrupt groups |
| [[HW_RA6M5_DataFlash]] | Data Flash map, 64-byte erase blocks, 4-byte write unit, scratch block allocation |

---

## Firmware Layer

| Note | Files |
|------|-------|
| [[FW_Clock_Driver]] | `drv_clk.h/.c` — CLK_Init, CLK_ModuleStart_SCI |
| [[FW_RWP_Driver]] | `drv_rwp.h/.c` — RWP_Unlock/Lock_Clock_MSTP |
| [[FW_UART_Driver]] | `drv_uart.h/.c` — UART_Init, SendChar (with timeout).  **CK board: `DEBUG_UART_CHANNEL=1` (SCI1/UART1, P709 TX / P708 RX) = J-Link OB VCOM** |
| [[FW_I2C_Driver]] | `drv_i2c.h/.c` — I2C_Init, bus recovery (9-clock) |
| [[FW_GPIO_Driver]] | `GPIO.h/.c` — GPIO_Config, invalid-port sentinel |
| [[FW_USB_Driver]] | `drv_usb.h/.c` — USBFS dual-mode (Device CDC log + Host CDC-ACM) |
| [[FW_FWUpdate_Receiver]] | `Middleware/FWUpdate/fwupdate_receiver.h/.c` — UART framed model transfer, CRC, ACK/NACK, Data Flash writes, **NVS metadata write after CMD_END** |
| [[FW_SafeReset]] | `src/safe_reset.h/.c` — NVS crash-reason persistence (Data Flash bytes [12..23]), SYSRESETREQ, stack canary scan |
| server_comm (code) | `src/server_comm.h/.c` — UART TX/RX split tasks: telemetry TX + FWUpdate RX |
| [[FW_TestFramework]] | `test_runner.h/.c` — shared test framework, currently used by IAQ and RTOS suites |
| [[FW_RTOS_Test]] | Khịch bản test preemptive scheduling |
| [[RCA_RTOS_Preemptive_Verification]] | Phân tích xác nhận PASS là hợp lệ |
| [[FW_TFLite_Integration]] | `src/iaq_predictor.cpp` — TFLite Micro inference wrapper, **OTA model boot selection from Data Flash vs compiled-in fallback** |

---

## BSP Layer

| Note | Files |
|------|-------|
| [[BSP_AHT20]] | `BSP/AHT20/bsp_aht20.h/.c` — AHT20 temp+humidity, I2C1 (SCL=P512, SDA=P511) |
| [[BSP_ZMOD4410]] | `BSP/ZMOD4410/bsp_zmod4410.h/.c` — ZMOD4410 raw-read BSP, PID/status/cmd/result path |

---

## RTOS Kernel

| Note | Covers |
|------|--------|
| [[FW_Scheduler_Core]] | TCB, Priority Bitmap (O(1) via `__CLZ`), Ready List, Task_Delay, Round-Robin |
| [[FW_Port_RA6M5]] | SysTick config (200 MHz, RVR=199999), NVIC SHPR3, Stack Frame init, EXC_RETURN |
| [[FW_Context_Switch]] | PendSV assembly, FPU S16-S31 lazy save/restore, SVC first-task launch |
| [[FW_Semaphore]] | Counting/Binary semaphores, priority-ordered wake, timeout support |
| [[FW_Software_Timer]] | One-shot/Auto-reload timers, Timer Daemon Task, ISR-minimal callbacks |
| [[FW_RTOS_Test]] | Guide and test logic for RTOS preemptive capabilities |

Configuration: `Config/rtos_config.h` — central config (cf. FreeRTOSConfig.h).
- `OS_DEFAULT_STACK_SIZE = 8192` (8 KB per task; raised from 4 KB for TFLite headroom)
- Stack canary `OS_STACK_CANARY_VALUE = 0xDEADC0DE` planted at `tcb->stack[0]` by `os_stack_init()`.
- `OS_StackOverflowCheck()` iterates all tasks; called from `safe_reset_check_stacks()` every 250 ms.

---

## Root Cause Analysis

| Note | Bug(s) |
|------|--------|
| [[RCA_SYSC_Redefinition]] | SYSC defined in both LPM.h and RWP.h |
| [[RCA_UART_BRR_SEMR]] | PCLKB=2MHz wrong; BGDM/ABCS bit positions swapped |
| [[RCA_UART_SSR_Manual_Clear]] | Manual SSR.TDRE clear harmful |
| [[RCA_UART_BaremetalNoOutput]] | MSTPCRB wrong addr + PSEL bit-shift (8→24) ✅ resolved |
| [[RCA_I2C_Init_Sequence]] | ICE set before IICRST; missing 0xE0 on ICBRL/ICBRH |
| [[RCA_I2C_Start_Hang]] | TEND polled after START — never fires, infinite hang |
| [[RCA_I2C_ACK_NACK]] | ACKBT written before ACKWP; wrong last-byte index |
| [[RCA_GPIO_Invalid_Port]] | Invalid port returned 0 (aliases PORT0) |
| [[RCA_USB_CDC_NoEnumeration]] | USB CDC reset loop / Code 43 — current focus is stable VBUS sampling and a DFLT-only FSP-like bus-reset path |
| [[RCA_Flash_HP_PE_Entry_Timeout]] | FLASH HP timeout traced to wrong RA6M5 HP base, missing `FWEPROR` enable, and wrong Data Flash command path |

---

## Project Status

Phase 1 (Drivers): All bugs resolved. Production-ready.

Phase 2 (RTOS Kernel): Full preemptive kernel with semaphores and software timers.

Phase 3 (BSP): Sensor drivers over verified I2C/UART layer.

| Milestone | Status |
|-----------|--------|
| Driver layer (Phase 1) | ✅ Complete |
| RTOS kernel (Phase 2) | ✅ Complete |
| Semaphores (Phase 2b) | ✅ Complete |
| Software Timers (Phase 2c) | ✅ Complete |
| UART baremetal debug | ✅ Resolved — MSTPCRB addr fix + PSEL bit-shift fix |
| AHT20 BSP (Phase 3) | ✅ Complete — [[BSP_AHT20]] |
| ZMOD4410 BSP (Phase 3) | 🟡 Partial — [[BSP_ZMOD4410]] raw transport only, no Renesas algorithm yet |
| AQI TFLite Inference | ✅ Complete — [[FW_TFLite_Integration]] |
| USBFS dual-mode driver | 🔴 Bug fixed — [[FW_USB_Driver]], [[HW_RA6M5_USBFS]], [[RCA_USB_CDC_NoEnumeration]] — pending hardware re-test |
| Hardware-in-loop test | 🔲 Pending |

---

## Legend

#done — implemented and verified
#in-progress — currently being worked
#todo — planned
#blocked — waiting on dependency
