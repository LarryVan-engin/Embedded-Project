# RCA_USB_CDC_NoEnumeration

Tags: #resolved #firmware #usb #cdc #rca

Final root-cause note for the USB CDC Device mode enumeration failure.
Historical host symptom: **Windows Code 43 — "Device Descriptor Request Failed"**.

Related docs: [[FW_USB_Driver]], [[HW_RA6M5_USBFS]]

---

## Final Resolution

The Code 43 failure was not a single defect. Enumeration only became reliable after a set of tightly-related fixes in clocking, EP0 control-IN handling, descriptor semantics, and startup timing.

Verified fixes now in the codebase:

- RA6M5 USBFS now gets a dedicated 48 MHz USB clock from PLL2/UCLK.
- EP0 control-IN now correctly handles:
	- short packets with `CFIFOCTR.BVAL`
	- multi-packet descriptor transfers via pipe0 `BEMP`
	- `CFIFOSEL` latch settle before CFIFO access
	- initial `CFIFOCTR.BCLR` before a fresh DCP IN transfer
	- `PID=NAK` before refilling pipe0 FIFO
- Device descriptors were simplified from composite/IAD-flavored CDC to classic single-function CDC ACM:
	- device class changed to CDC (`0x02`)
	- IAD removed
	- configuration total length reduced from `0x4b` to `0x43`
- `USB_PollEvents()` now drains pending USB sources in priority order within one invocation instead of servicing one source and returning immediately.
- Heavy non-USB startup work was moved out of the default-address enumeration window so TFLM init no longer collides with the last configuration-descriptor transfer.

The follow-up open-port latency was also reduced after enumeration was fixed:

- post-enumeration USB trace chatter is no longer dumped verbosely over blocking UART during CDC ACM port-open negotiation
- USB CDC test traffic is now gated on host-side CDC activity instead of sending unsolicited BULK IN test frames immediately after `SET_CONFIGURATION`
- USB polling test task priority was raised above the background test tasks

Result:

- Windows enumeration succeeds
- the CDC device appears and can be opened by the serial tool
- the remaining work is now performance/usability tuning, not Code 43 recovery

---

## Symptom

Observed trace before the USB clock fix:

```
[USB CDC TRACE] state=PULLUP_ENABLED v0=0x411 v1=0xa v2=0x80
[USB CDC TRACE] DVST is0=0x10c0 SYSSTS0=0x1 DVSTCTR0=0x0 addr=0 cfg=0
[USB CDC TRACE] DVST is0=0x5090 SYSSTS0=0x0 DVSTCTR0=0x2 addr=0 cfg=0
[USB CDC TRACE] state=BUS_RESET_REARM v0=0x40 v1=0x0 v2=0x40
[USB CDC TRACE] DVST is0=0x1090 ... state=BUS_RESET_REARM ...
[USB CDC TRACE] DVST is0=0x10d0 SYSSTS0=0x1 DVSTCTR0=0x2 addr=0 cfg=0
```

The device never reached `CTRT`, `SETUP`, `GET_DESCRIPTOR`, `SET_ADDRESS`, or `SET_CONFIGURATION` traces.

---

## Follow-up Trace After UCLK + Initial EP0 Fixes

After adding RA6M5 USB 48 MHz clocking and the first EP0 short-packet fix, the device now gets much further:

```
[USB CDC TRACE] SETUP GET_DESCRIPTOR ... wValue=0x0100 ... wLen=64
[USB CDC TRACE] state=GET_DESCRIPTOR v0=0x100 v1=0x12 v2=0x0
[USB CDC TRACE] state=STATUS_STAGE CTSQ=RDSS(2) ...
[USB CDC TRACE] SETUP GET_DESCRIPTOR ... wValue=0x0200 ... wLen=255
[USB CDC TRACE] state=GET_DESCRIPTOR v0=0x200 v1=0x4b v2=0x0
[USB CDC TRACE] SETUP GET_DESCRIPTOR ... wValue=0x0200 ... wLen=255
```

So the failure mode has changed:

- the host can now reach EP0 setup and receive at least the device descriptor path
- the host now requests the configuration descriptor (`0x0200`, length `0x4b = 75`)
- Windows still does **not** progress to `SET_ADDRESS`
- the configuration descriptor request is retried, which points to an EP0 control-IN completion problem or malformed data on that stage

---

## Corrected Decode

`DVSQ` must be decoded from `INTSTS0 & 0x0070`, not from the full 16-bit word.

| is0 | `is0 & 0x0070` | Decoded state |
|-----|----------------|---------------|
| `0x10c0` | `0x0040` | `SPD_POWR` |
| `0x5090` | `0x0010` | `DFLT` |
| `0x1090` | `0x0010` | `DFLT` |
| `0x10d0` | `0x0050` | `SPD_DFLT` |

That correction matters because an earlier RCA incorrectly treated `0x5090` as `SPD_DFLT`.

---

## Historical Intermediate State

Two fixes described in the earlier RCA are already present in current source:

1. `USB_PollEvents()` already re-arms EP0 on `DFLT` and `SPD_DFLT`.
2. `USB_Init()` already enables `BRDYENB[0]` for DCP / EP0.

The EP0 control path also still matches the known-good RA USBFS sequence in repo notes:
- re-read `INTSTS0` on `CTRT`
- branch by `CTSQ`
- clear `VALID` before reading `USBREQ/USBVAL/USBINDX/USBLENG`
- finish status stages with `BUF + CCPL`

So the old RCA is stale as a primary explanation for the current reset loop.

---

## Historical Working Hypothesis

The controlling code path is no longer pull-up or USB root clock. The remaining fault is now centered on
**device-side EP0 control-IN startup/continuation for descriptor reads**.

### H1: Control-IN startup still differs from FSP in two important ways

Local FSP comparison showed two more differences in the custom EP0 IN path:

1. FSP clears CFIFO state with `BCLR` before starting a new DCP IN transfer.
2. FSP waits for `CFIFOSEL.ISEL/CURPIPE` to latch before assuming CFIFO is pointing at pipe 0 in IN direction.

The custom code already fixed short-packet `BVAL` handling and multi-packet continuation, but it still started
a new control-IN response without that initial FIFO-state clear and without waiting for the pipe/direction select to settle.

That is a plausible explanation for the new trace pattern:

1. first device descriptor transfer now often succeeds
2. configuration descriptor transfer starts
3. host retries the same `GET_DESCRIPTOR(CONFIGURATION)` request
4. enumeration never advances to `SET_ADDRESS`

### H2: If EP0 startup is fully corrected and retries remain, the next suspect is descriptor content

The current 75-byte CDC ACM configuration descriptor looks structurally plausible at a glance, but if Windows still resets after the latest CFIFO/DCP fix, the next likely branch is descriptor validity rather than clocking.

The tell would be:

- control read stages complete consistently
- no obvious EP0 retransmission issue remains
- host still resets before `SET_ADDRESS` or configuration selection

---

## Historical Patch Under Test

The code under test now contains three rounds of fixes in `drv_usb.c` / clock init:

1. RA6M5 USB clock path fixed: PLL2/UCLK now provides a dedicated 48 MHz USB clock.
2. EP0 short packet + continuation fixed: short DCP IN packets assert `BVAL`, and multi-packet descriptor replies continue on pipe 0 via `BEMP`.
3. EP0 startup aligned further with FSP: the driver now waits for `CFIFOSEL` latch on pipe select and clears CFIFO with `BCLR` before the first packet of a new control-IN response.

These changes are intended to eliminate the remaining gap between:

- “descriptor bytes exist in memory”
- and “descriptor data is presented on EP0 exactly as the USBFS peripheral expects”

---

## Historical Re-Test Signal

After reflashing the latest patch, the useful success signal is no longer just “we saw `GET_DESCRIPTOR`”.
The next good trace should instead progress past the current stall point:

```
[USB CDC TRACE] SETUP GET_DESCRIPTOR ...
[USB CDC TRACE] SETUP GET_DESCRIPTOR ... CONFIGURATION ...
[USB CDC TRACE] state=GET_DESCRIPTOR v0=0x200 v1=0x4b ...
[USB CDC TRACE] SETUP SET_ADDRESS ...
[USB CDC TRACE] SETUP SET_CONFIGURATION ...
```

If the host still repeats `GET_DESCRIPTOR(CONFIGURATION)` and resets without any `SET_ADDRESS`, the next debug step should focus on descriptor content or on adding wire-level EP0 continuation traces.

---

## Status

| Item | Status |
|------|--------|
| USB 48 MHz UCLK fix applied | ✅ |
| EP0 short-packet `BVAL` + pipe0 continuation patch applied | ✅ |
| EP0 `CFIFOSEL` latch wait + initial `BCLR` patch applied | ✅ |
| CDC descriptor simplification applied | ✅ |
| USB poll-source drain fix applied | ✅ |
| Startup timing collision removed | ✅ |
| Hardware re-test for Code 43 | ✅ Passed |
| CDC open-latency optimization applied | ✅ |

---

## References

- RA6M5 USBFS `DVSQ[6:4]` decode rules
- [[HW_RA6M5_USBFS]]
- [[FW_USB_Driver]]
