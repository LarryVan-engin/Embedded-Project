# RCA_Flash_HP_PE_Entry_Timeout

Tags: #in-progress #firmware #flash #dataflash #rca

Root-cause note for the Data Flash self-test failure:
`[FLASH NVS TEST] FAIL status=2` where `status=2` is `FLASH_HP_ERR_TIMEOUT`.

Related docs: [[HW_RA6M5_DataFlash]], [[FW_FWUpdate_Receiver]]

---

## Symptom

The one-shot scratch-block test in `test_flash_nvs.c` reported a timeout before completing erase/write.

Project clock context already confirms `FCLK = 50 MHz`, so the failure was not explained by a missing flash clock.

---

## Current Root Cause

The old custom FLASH HP driver was not merely waiting on the wrong entry condition. It also mismatched the
RA6M5 local CMSIS/FSP definitions in multiple places, which made both erase and program sequencing unsafe.

The key mismatches were:

1. Wrong FLASH HP register base.
	- Correct RA6M5 HP base: `0x407FE000`
2. Wrong command issuing area.
	- Correct command area: `0x407E0000`
3. Wrong register offsets and status bit meanings.
	- `FASTAT`, `FSTATR`, `FCPSR`, and `FPCKAR` were not mapped to the real RA6M5 HP layout.
4. Wrong command path.
	- `FCMDR` was treated like a writable command register, but on this part it is a read-only command/status history register.
5. Wrong program command format for Data Flash.
	- RA6M5 HP Data Flash programs one 4-byte unit at a time and the command count is two halfwords, not an arbitrary byte count.
6. Incomplete P/E entry model.
	- Data Flash P/E entry and exit must be confirmed through `FENTRYR`, not through an LP-style `FPMCR` sequence.
7. Missing write/erase permission enable.
	- The local FSP path enables `R_SYSTEM->FWEPROR = 0x01` before Data Flash P/E operations.
	- Without this, the hardware reports `FSTATR.FLWEERR` and `FASTAT.CMDLK`, which matches the latest board log.

So the timeout symptom could originate from several bad assumptions in the old driver, not just the initial mode transition.

---

## Fix Applied

The current patch aligns the custom driver to the local RA6M5 CMSIS/FSP implementation while keeping the custom API.

`drv_flash_hp.h` now uses:

1. `FLASH_HP_BASE = 0x407FE000`
2. `FLASH_HP_CMD_BASE = 0x407E0000`
3. Correct RA6M5 HP offsets for `FASTAT`, `FSTATR`, `FCPSR`, and `FPCKAR`
4. Correct HP error masks, including `CMDLK`

`drv_flash_hp.c` now:

1. Waits for idle before entering P/E mode
2. Enables program/erase permission through `FWEPROR = 0x01`
3. Notifies the sequencer of `FCLK = 50 MHz` through `FPCKAR`
4. Enters and exits Data Flash P/E mode by writing `FENTRYR` and waiting on the resulting `FENTRYR` state
5. Sends `STATUS_CLEAR`, `ERASE`, `PROGRAM`, and confirm commands through the real command issuing area
6. Uses `FCPSR = 1` for the erase path
7. Programs Data Flash in strict 4-byte units:
	- `PROGRAM`
	- halfword count `2`
	- two 16-bit data writes
	- `PROGRAM_CFM`

This is materially closer to the FSP RA6M5 FLASH HP flow and removes the earlier LP-style assumptions.

---

## Added Diagnostics

`test_flash_nvs.c` now captures the failure snapshot before `flash_hp_exit()` overwrites the live state.

Logged fields:

- `FSTATR`
- `FENTRYR`
- `FASTAT`
- `FSADDR`
- `FCMDR`

Example format:

```
[FLASH NVS TEST] FAIL status=%u stage=%s FSTATR=0x%x FENTRYR=0x%x FASTAT=0x%x FSADDR=0x%x FCMDR=0x%x
```

This makes the next board run able to distinguish:

- failure entering P/E mode
- erase timeout
- write timeout
- access-error / command-lock conditions

---

## Status

| Item | Status |
|------|--------|
| FCLK source checked (`50 MHz`) | ✅ |
| HP base / command area corrected | ✅ |
| Write/erase protection enable corrected | ✅ |
| Status / error decoding corrected | ✅ |
| Data Flash program format corrected | ✅ |
| Flash self-test diagnostics expanded | ✅ |
| Firmware rebuild after patch | ✅ |
| Hardware re-test | 🔲 Pending |