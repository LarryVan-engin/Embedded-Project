# FW_USB_Driver

Tags: #firmware #usb #cdc #host #device

Dual-mode USBFS firmware driver for RA6M5.

Implemented files:
- `Driver/Include/drv_usb.h`
- `Driver/Source/drv_usb.c`
- `src/test/test_usb.h`
- `src/test/test_usb.c`

Hardware dependency: [[HW_RA6M5_USBFS]]

---

## Scope

### Function 1: USB Device CDC (Logging)

- Configure USBFS in Device mode
- Apply manual USBFS pin mux before enabling the engine:
  - `P407` as `USBFS_VBUS` peripheral input
- Provide CDC descriptors:
  - Device Descriptor
  - Classic full-speed CDC ACM Configuration Descriptor (no IAD)
  - String descriptors (`LANGID`, manufacturer, product, serial)
  - Communication Interface + Data Interface
  - Notification interrupt IN + data bulk OUT/IN endpoints
- EP0 handles:
  - Standard requests: `GET_STATUS`, `GET_DESCRIPTOR`, `SET_ADDRESS`, `GET_CONFIGURATION`, `SET_CONFIGURATION`, `GET_INTERFACE`, `SET_INTERFACE`
  - CDC class requests: `SET_LINE_CODING`, `GET_LINE_CODING`, `SET_CONTROL_LINE_STATE`, `SEND_BREAK`
- Bulk IN log stream using:
  - `USB_Dev_Write(...)`
  - `USB_Dev_Printf(...)`
  - `USB_Dev_IsHostReady(...)` — gate TX on host CDC open; cleared on explicit CDC close or stalled BULK IN
- Event path supports polling and IRQ hook:
  - `USB_PollEvents()`
  - `USBI0_IRQHandler()`, `USBI1_IRQHandler()`

### Function 2: USB Host CDC-ACM (SIM Module)

- Configure USBFS in Host mode
- Apply manual USBFS pin mux before enabling the engine:
  - `P407` as `USBFS_VBUS` peripheral input
  - `P501` as `USBFS_OVRCURA` peripheral input
- Enable VBUS switch (`P500`, symbolic `USBFS_VBUS_EN`)
- Bare-metal enumeration flow:
  1. Attach detect by line state (`SYSSTS0.LNST`)
  2. Bus reset (`DVSTCTR0.USBRST`)
  3. Read Device Descriptor (EP0 control transfer)
  4. `SET_ADDRESS`
  5. Read full Configuration Descriptor and parse endpoints
  6. `SET_CONFIGURATION`
- Data API:
  - `USB_Host_Send_AT_Command(char *cmd)` via Bulk OUT
  - `USB_Host_Read_Response(uint8_t *buffer, uint32_t len)` via Bulk IN

---

## API

```c
drv_status_t USB_Init(usb_mode_t mode);
void USB_Deinit(void);

void USB_PollEvents(void);
void USBI0_IRQHandler(void);
void USBI1_IRQHandler(void);

drv_status_t USB_Dev_Write(const uint8_t *data, uint32_t len);
drv_status_t USB_Dev_Printf(const char *fmt, ...);
uint8_t USB_Dev_IsHostReady(void);

drv_status_t USB_Host_Enumerate(void);
drv_status_t USB_Host_Send_AT_Command(char *cmd);
int32_t USB_Host_Read_Response(uint8_t *buffer, uint32_t len);
const usb_host_cdc_device_t *USB_Host_GetDeviceInfo(void);
```

Return conventions:
- `DRV_OK`: transaction completed
- `DRV_TIMEOUT`: timeout on attach, setup, FIFO ready, BRDY/BEMP wait
- `DRV_ERR`: invalid mode/state, descriptor parse failure, malformed transfer

---

## Driver Internals

### EP0 Control Handling (Device)

Setup packet is read from:
- `USBREQ`, `USBVAL`, `USBINDX`, `USBLENG`

Control-stage routing in `USB_PollEvents()`:
- `CTRT` is re-sampled by reading `INTSTS0` again before decoding control stage
- Only `CTSQ = RDDS/WRDS/WRND` is treated as a new setup request
- `VALID` is cleared before reading setup regs so stale EP0 state is not re-dispatched
- `CTSQ = RDSS/WRSS` only releases EP0 status stage with `BUF + CCPL`
- pending USB sources are drained in priority order within one `USB_PollEvents()` call so EP0 state transitions are not deferred to a later scheduler tick

Control-IN transmit details for EP0 / DCP:
- short packets assert `CFIFOCTR.BVAL`
- multi-packet replies continue on pipe0 `BEMP`
- `CFIFOSEL` latch is waited after selecting DCP IN
- `CFIFOCTR.BCLR` is used before a fresh control-IN transfer
- pipe0 is driven to `PID=NAK` before FIFO refill

Dispatch rules:
- `bmRequestType` standard (`0x00/0x80`): standard handler
- `bmRequestType` class (`0x21/0xA1`): CDC class handler
- Unsupported requests: STALL on DCP

Line coding is persisted in `g_line_coding` for debug tracking.

Device-side pipe layout:
- PIPE1 = CDC data BULK IN (EP1 IN)
- PIPE2 = CDC data BULK OUT (EP2 OUT)
- PIPE6 = CDC notification INT IN (EP3 IN)

**PIPE1 / PIPE2 buffer mode: single-buffer (DBLB=0)**
Double-buffer mode (DBLB=1) causes bank-pointer desync when packets are sent
at low frequency (>100 ms gap).  The CPU-side CFIFO bank counter and the
hardware TX bank counter diverge between calls, causing hardware to re-transmit
the previous packet from the stale bank.  Single-buffer mode is correct for
debug logging usage. See BUG-USB-05.

Host-readiness note:
- `SET_CONFIGURATION` marks the device configured
- the driver separately latches host-side CDC activity on valid class requests (`GET/SET_LINE_CODING`, `SET_CONTROL_LINE_STATE`, `SEND_BREAK`)
- an explicit `SET_CONTROL_LINE_STATE` close (`wValue = 0`) or a stalled BULK IN write clears the latched host-ready state immediately, so the logger does not keep blocking for many seconds after the terminal closes
- `USB_Dev_IsHostReady()` returns true only after both conditions are met, which is useful for gating test/debug traffic

### Host Enumeration Implementation

Control transfer helper stages:
1. Program setup packet regs
2. Trigger setup (`DCPCTR.SUREQ`)
3. Wait setup completion (timeout-protected)
4. For IN data stage: poll `BRDYSTS`, read `CFIFO`

Endpoint discovery:
- Parse configuration descriptor blocks
- Filter endpoint descriptors with `bmAttributes == BULK`
- Record first BULK IN and BULK OUT endpoints + max packet sizes

Pipe setup:
- PIPE1 = host BULK OUT
- PIPE2 = host BULK IN

---

## Error Handling and Debug Tracking

Implemented protections:
- Global timeout loops using `DRV_TIMEOUT_TICKS`
- FIFO ready timeout (`CFIFOCTR.FRDY`)
- BRDY/BEMP wait timeout during TX/RX
- Enumeration sanity checks (descriptor length and endpoint presence)

Recommended debug probes:
- `INTSTS0`, `BRDYSTS`, `BEMPSTS`, `NRDYSTS`
- `SYSSTS0.LNST` in attach/detach scenarios
- `DCPCTR` when setup stage stalls

---

## RTOS Test Integration

Integration point: `src/main.c`

Provided test hooks:
- `test_usb_cdc_logger_init()`
  - Creates one RTOS task
  - Polls USB events every 1 ms to service control/data state changes promptly
  - Only enabled when `OS_USB_CDC_TEST_ENABLE=1` (UART backend only)
  - Sends periodic CDC log frames only after `USB_Dev_IsHostReady()` becomes true
  - When USB CDC is used as debug backend (`OS_DEBUG_BACKEND_USB_CDC=1`), this task must be disabled — USB is owned by `usb_svc` in main.c
- `test_usb_host_descriptor_init()`
  - Creates one one-shot enumeration task in host mode
  - Enumerates attached CDC-ACM device / SIM module
  - Logs Device Descriptor summary and parsed BULK endpoints over `debug_print`

---

## 2026-05-08 Update

## 2026-05-09 Update

### USB CDC as production debug backend
- `OS_DEBUG_BACKEND_USB_CDC=1` routes all `debug_print()` calls through the USB
  CDC BULK IN pipe using `USB_Dev_Write` (blocking, waits for BEMP).
- `OS_DEBUG_BACKEND_UART` and `OS_USB_CDC_TEST_ENABLE` must both be 0 when this
  is active — USB ownership belongs to `usb_svc` task created in `main.c`.
- `main.c` calls `USB_Init(USB_MODE_DEVICE_CDC)` before the RTOS starts, then
  creates a `usb_svc` task (priority 2) that calls `USB_PollEvents()` every 1 ms.
- `usb_device_wait_bulk_in_complete()` also calls `USB_PollEvents()` internally
  every `USB_CDC_DEV_TX_POLL_STRIDE` ticks, so BEMP can fire even if `usb_svc`
  is preempted during a blocking write.

### Fire-and-forget TX path removed
- `USB_Dev_Write_Log` and `USB_Dev_Printf_Log` deleted from driver and header.
- Fire-and-forget: no BEMP wait, 64-byte truncation, no host-ready check → root
  cause of silent packet drops and seq repetition under low-frequency traffic.
- Callers that need non-blocking TX have no use case in this codebase.
---

## Flow Map (FW Perspective)

`USB_Init(mode)`
→ `manual USBFS pin mux bring-up (P407 common, P501 host overcurrent)`
→ `SYSCFG role + board power path`
→ `EP0 setup engine`
→ `Device: descriptors/control + log bulk IN`
→ `Host: enumerate + parse endpoints + configure pipes`
→ `AT send/read over BULK OUT/IN`
→ `USB_PollEvents or USBI0/USBI1 handlers`

---

## Known Issues / Bug History

### BUG-USB-01: Code 43 — Device Descriptor Request Failed (FIXED, verified)

**Symptom:** Windows reports "Unknown USB Device (Device Descriptor Request Failed)".
Device never logs CTRT/SETUP events. DVST loops between POWR/BUS_RESET indefinitely.

**Root Cause:** `USB_PollEvents()` DVST handler only called `usb_device_bus_reset()` when
`DVSQ == USB_DS_DFLT (0x0010)`. The RA6M5 USBFS hardware first sets `DVSQ = SPD_DFLT (0x0050)`
during bus reset + speed detection. That event was silently ignored, leaving EP0 in stale
state when the host sent `GET_DESCRIPTOR`. See [[RCA_USB_CDC_NoEnumeration]].

**Fix applied in `drv_usb.c`:**
- DVST handler extended to also trigger `usb_device_bus_reset()` on `SPD_DFLT (0x0050)`
  and `SPD_POWR (0x0040)`.
- `BRDYENB[0]` now enabled at device-mode init to make SET_LINE_CODING data stage
  interrupt-driven rather than polled.

**Status:** Fixed and hardware-verified.

### BUG-USB-02: Slow CDC ACM Port Open / Connect Latency (FIXED)

**Symptom:**
- Windows enumeration succeeds, but opening the CDC COM port from the serial tool can take multiple seconds.
- Logs show a burst of repeated `GET_LINE_CODING`, `SET_LINE_CODING`, and `SET_CONTROL_LINE_STATE` requests during port open.

**Root Cause:**
- The USB test task was polling in task context and had lower priority than background tasks.
- At the same time, verbose USB trace output was being drained over blocking UART during the CDC ACM open handshake, stretching the time between successive `USB_PollEvents()` calls.
- The USB CDC test logger also attempted unsolicited BULK IN writes before the host had actually started CDC class negotiation.

**Fix applied:**
- raised the USB polling test task priority above the background test tasks
- gated CDC test traffic on `USB_Dev_IsHostReady()` instead of only `USB_Dev_IsConfigured()`
- limited device-mode CDC BULK IN completion waits to a short fail-fast window and clear the pending pipe state on timeout so disconnect/close recovery happens in under a second instead of tens of seconds
- drop the latched host-ready state immediately on explicit CDC close (`SET_CONTROL_LINE_STATE = 0`) and on stalled writes so the logger stops retrying against a closed port

**Status:** Fixed and hardware-verified.

---

### BUG-USB-03: ZLP on EP0 / SET_CONFIGURATION dropped on reconnect (FIXED)

**Symptom:** On second connect (unplug + replug without board reset), `configured=1`
never fires.  UART shows DVST events loop but no `SW_CONFIGURED` state.

**Root Cause:** `USB_PollEvents()` DVST and BEMP interrupt bits were set
simultaneously in `INTSTS0`.  The DVST handler called `continue`, so the loop
restarted with the same `is0` snapshot.  The BEMP for EP0 was processed in the
next pass but the gap left FIFO empty with `PID=BUF`, causing hardware to
auto-send a ZLP.  The host interpreted this as the status stage completing
before the CPU had loaded the descriptor response, aborting the control transfer.

**Fix applied in `drv_usb.c`:**
- BEMP handler for pipe 0: immediately set `PID=NAK` before clearing BEMPSTS
  and before refilling the FIFO, preventing the ZLP window.

**Status:** Fixed and hardware-verified.

---

### BUG-USB-04: Stuck enumeration on reconnect — host gives up, device stays attached (FIXED)

**Symptom:** After the host abandons enumeration (COM port unplug before
`SET_CONFIGURATION`), the device stays attached with `configured=0`.  The host
never re-enumerates on the next open attempt.

**Root Cause:** When the host gives up during enumeration, it puts the bus into
SUSPEND state.  The device remained with `DPRPU` asserted and no way to force
a fresh attachment cycle.

**Fix applied in `drv_usb.c`:**
- Added `g_usb_enum_suspend_count` counter in `usb_device_update_pullup()`.
- If `DVSQ` bit 6 (SUSPEND) is set while `configured==0` for 200 consecutive
  polls (~200 ms), call `usb_device_force_detach()` to pull DPRPU low.
- VBUS absent debounce (`g_usb_vbus_absent_count`, 50 ms) prevents false detach
  during normal enumeration.

**Status:** Fixed and hardware-verified.

---

### BUG-USB-05: BULK IN seq repeated — DBLB double-buffer bank desync (FIXED)

**Symptom:** USB terminal shows `seq=1 tick=5526` repeated indefinitely.  UART
log shows `TX OK seq=2`, `TX OK seq=3`, etc. — confirming the host receives the
old data while the driver thinks transmit succeeded.

**Root Cause:** PIPE1 was configured with `DBLB=1` (double-buffer mode).
Hardware maintains two physical FIFO banks (B0, B1) with its own TX bank
counter.  Between `USB_Dev_Write` calls spaced 1 second apart, the hardware TX
bank counter and the CPU-side CFIFO access bank diverge: CPU writes seq=N+1 to
B1 but hardware retransmits seq=N from B0.  An ACLRM workaround (reset both
banks) partially helped (fixed alternating ERR/OK) but could not fully prevent
the desync because the CFIFO access bank pointer is not reset by ACLRM.

**Root cause conclusion:** DBLB is designed for continuous streaming where one
bank fills while the other transmits.  For debug logging with >100 ms between
packets, DBLB is fundamentally incompatible.

**Fix applied in `drv_usb.c`:**
- Removed `USB_PIPECFG_DBLB` from PIPE1 and PIPE2 PIPECFG in both `USB_Init()`
  and the re-attach pipe reinit inside `usb_device_update_pullup()`.
- Removed the ACLRM workaround comment from `USB_Dev_Write` preamble (ACLRM
  call retained as general FIFO cleanup, comment updated).

**Status:** Fixed, build verified. Flash and hardware validation pending.
- suppressed verbose post-enumeration USB trace chatter by default, keeping only error/stall visibility during normal open/close flows

**Status:** Applied. Re-test expected to show significantly faster COM open behavior.
