# HW_RA6M5_DataFlash

Tags: #in-progress #hardware #flash #dataflash

RA6M5 Data Flash geometry and the project-specific allocation used by `drv_flash_hp` and the FWUpdate receiver.

---

## Physical Geometry

| Item | Value |
|------|-------|
| Read-mapped start address | `0x08000000` |
| Read-mapped end address | `0x08001FFF` |
| End (exclusive) | `0x08002000` |
| Total size | `0x2000` bytes (8 KB) |
| Erase block size | 64 bytes |
| Block count | 128 |
| Minimum Data Flash write unit | 4 bytes |
| Maximum program command size | 256 bytes |

Project sources:
- `memory_regions.ld` maps `DATA_FLASH_START = 0x08000000` and `DATA_FLASH_LENGTH = 0x2000`
- `drv_flash_hp.h` defines `DATA_FLASH_BLOCK_SIZE = 64` and `DATA_FLASH_WRITE_WORD_SIZE = 4`
- Renesas FSP feature table for RA6M5 also reports `BSP_FEATURE_FLASH_HP_DF_WRITE_SIZE = 4`

Important distinction:
- `128 bytes` is the UART transport chunk size used by FWUpdate.
- `4 bytes` is the actual RA6M5 Data Flash programming alignment requirement.
- There is no project evidence that RA6M5 Data Flash requires a 128-bit minimum program unit.

---

## Current Project Allocation

| Region | Start | End | Size | Purpose |
|--------|-------|-----|------|---------|
| FWUpdate image area | `0x08000000` | `0x08001FBF` | 8128 bytes | IAQ model image written by `fwupdate_receiver` |
| NVS scratch block | `0x08001FC0` | `0x08001FFF` | 64 bytes | One-shot RTOS flash self-test (`test_flash_nvs`) |

The final 64-byte block is intentionally reserved so the flash test task does not erase or overwrite the model image area.

---

## Programming Rules

1. Enter Data Flash P/E mode with `flash_hp_init()` before erase/write.
2. Erase in 64-byte blocks only.
3. Write lengths and addresses must be 4-byte aligned.
4. If payload length is not a multiple of 4, pad the tail with `0xFF` before `flash_hp_write()`.
5. Exit P/E mode with `flash_hp_exit()` before normal read-back verification.

FLASH HP P/E transition note:
- On this RA6M5 FLASH HP path, entry/exit confirmation is taken from `FENTRYR`.
- Enter Data Flash P/E: write `0xAA80`, then wait until `FENTRYR == 0x0080`.
- Exit to read mode: write `0xAA00`, then wait until `FENTRYR == 0x0000`.
- See [[RCA_Flash_HP_PE_Entry_Timeout]] for the timeout bug caused by waiting on the wrong condition.

That rule is applied in two places:
- `fwupdate_receiver.c` pads incoming UART payloads to the 4-byte write unit.
- `test_flash_nvs.c` pads a 21-byte sample pattern before programming the scratch block.

---

## Integration Notes

- The flash self-test uses `DATA_FLASH_LAST_BLOCK_ADDR`.
- `FWUPDATE_MAX_MODEL_LEN` is reduced from 8192 to 8128 bytes to preserve the scratch block.
- Read-back verification is done from the read-mapped Data Flash window after leaving P/E mode.