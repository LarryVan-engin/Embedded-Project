# FW_FWUpdate_Receiver

Tags: #in-progress #firmware #uart #fwupdate

UART firmware-update receiver for RA6M5. Files: `Middleware/FWUpdate/fwupdate_receiver.h`, `Middleware/FWUpdate/fwupdate_receiver.c`.

Peer implementation:
- ESP32-C6 sender: `ESP32C6_SENDER/ESP32C6_SENDER.ino`

Flash backend:
- `drv_flash_hp.h`, `drv_flash_hp.c`
- Data Flash map: [[HW_RA6M5_DataFlash]]

---

## UART Link

| Item | Value |
|------|-------|
| RA6M5 channel | Same as legacy debug UART (`OS_DEBUG_UART_CHANNEL`, default SCI7) |
| RA6M5 pins | Same wiring as old debug UART channel |
| ESP32-C6 channel | UART1 |
| ESP32-C6 pins | TX=GPIO17, RX=GPIO16 |
| Baud rate | 115200 |
| Framing | 8-N-1 |

The receiver uses polling in `fwupdate_receiver_run()` to collect RX bytes, then parses frames and performs flash operations in normal context.

---

## 2026-05-08 Integration Update

- FWUpdate RX now reuses the old debug UART channel for easier wiring with ESP32-C6 sender.
- RX path changed from fixed SCI2 RXI interrupt to polling mode, reducing NVIC/vector coupling when channel assignment changes.
- FWUpdate is now intended to run inside a dedicated RX task (`server_comm` RX task), while TX telemetry runs in a separate TX task.
- Receiver now accepts a new `CMD_START` after `DONE` state without requiring MCU reset.

---

## Handshake Sequence

1. `CMD_START (0x01)`
   Payload: 4-byte big-endian total image length.
   Receiver action: validate length, enter flash P/E mode, erase required Data Flash blocks, reply ACK/NACK.

2. `CMD_DATA (0x02)` repeated
   Payload: `1..128` bytes.
   Receiver action: validate cumulative length, update running image CRC, pad to the 4-byte flash write unit if needed, program Data Flash, reply ACK/NACK.

3. `CMD_END (0x03)`
   Payload: 2-byte big-endian full-image CRC.
   Receiver action: ensure all declared bytes were received, compare running CRC, exit P/E mode, recompute CRC from flash-mapped memory, reply ACK/NACK.

Sender-side timing currently used by the project:
- Inter-frame delay: 20 ms
- ACK/NACK timeout: 500 ms for `CMD_START` and `CMD_DATA`
- `CMD_END` timeout: 1000 ms

---

## Frame Format

Every request and response uses the same framing:

```text
[STX][CMD][LEN_MSB][LEN_LSB][DATA...][CRC_MSB][CRC_LSB][ETX]
```

| Field | Size | Notes |
|-------|------|-------|
| `STX` | 1 byte | `0x02` |
| `CMD` | 1 byte | request or response command |
| `LEN_MSB/LEN_LSB` | 2 bytes | payload length, big-endian |
| `DATA` | 0..128 bytes | request payload, or 1-byte ACK/NACK payload |
| `CRC_MSB/CRC_LSB` | 2 bytes | CRC-16-CCITT over `CMD + LEN + DATA` |
| `ETX` | 1 byte | `0x03` |

Responses:
- `CMD_ACK (0xAA)`: 1-byte payload = echoed accepted command
- `CMD_NACK (0xFF)`: 1-byte payload = error reason code

The ESP32 response parser now validates all of the following before accepting a reply:
- exact response length = 7 bytes after `STX`
- command is `ACK` or `NACK`
- payload length field is exactly `0x0001`
- CRC matches over `[CMD][LEN_MSB][LEN_LSB][DATA]`

---

## CRC-16-CCITT Rules

Both endpoints use the same CRC configuration:

| Parameter | Value |
|-----------|-------|
| Polynomial | `0x1021` |
| Initial value | `0xFFFF` |
| Reflection | none |
| Final XOR | none |
| Scope | `CMD + LEN_MSB + LEN_LSB + DATA...` |

Current synchronization fixes:
- RA6M5 ACK/NACK frames now include the 1-byte payload in the CRC calculation.
- ESP32 and RA6M5 both use the same frame CRC and full-image CRC rules.

---

## Error Handling

| NACK Code | Meaning | Typical trigger |
|-----------|---------|-----------------|
| `0x01` | CRC error | corrupted frame payload/header |
| `0x02` | sequence error | `CMD_DATA` before `CMD_START`, premature `CMD_END`, extra data after total length |
| `0x03` | flash error | erase/program failure or write beyond reserved image area |
| `0x04` | length error | declared length too large or chunk exceeds remaining image bytes |
| `0x05` | verify error | flash read-back CRC mismatch |
| `0x06` | image CRC error | full-image CRC from sender does not match receiver |

Receiver recovery rules:
- Any invalid request frame returns to `IDLE` and waits for the next `STX`.
- CRC-invalid frames send `NACK_CRC`.
- ACK/NACK frames are generated with a 1-byte payload and a CRC that covers that payload.

---

## Flash Alignment Handling

- UART transport chunk size is `128 bytes` maximum.
- RA6M5 Data Flash write alignment is `4 bytes`.
- The receiver pads only the final non-aligned write tail with `0xFF`.
- The running image length remains the original byte count; padding bytes are not included in the image CRC.

This keeps the transport protocol independent from the underlying flash programming unit.

---

## NVS Metadata Persistence (OTA Validity Record)

After a successful `CMD_END` (CRC + read-back verified), the receiver writes a
64-byte validity record into the **last** 64-byte Data Flash block at
`DATA_FLASH_LAST_BLOCK_ADDR` (`0x08001FC0`).

This record survives power cycles and resets, enabling `IAQ_Init()` to select the
OTA model from Data Flash instead of the compiled-in fallback on every subsequent boot.

### NVS Block Layout (64 bytes)

```
Offset  Size  Content
------  ----  -------
 0.. 3   4    Magic 0xDE 0xAD 0xBE 0xEF  — OTA model present
 4.. 7   4    model_len  (big-endian uint32_t)
 8.. 9   2    model_crc  (CRC-16-CCITT, big-endian uint16_t)
10..11   2    0xFF (reserved)
12..15   4    Safe-reset magic 0xC0 0xFF 0xEE 0x00  (written by safe_reset.c)
16..19   4    Reset reason code (big-endian uint32_t)
20..23   4    Cumulative reset count (big-endian uint32_t)
24..63  40    0xFF (reserved)
```

### Implementation

`fwupdate_write_nvs_metadata(uint32_t model_len, uint16_t model_crc)` in
`fwupdate_receiver.c`:

1. Reads the existing 64-byte block into a local buffer (memory-mapped read,
   no P/E required).
2. Sets bytes `[0..3]` to `0xDEADBEEF` (big-endian).
3. Writes `model_len` into bytes `[4..7]` (big-endian).
4. Writes `model_crc` into bytes `[8..9]` (big-endian), zeroes `[10..11]`.
5. Preserves bytes `[12..63]` (safe-reset marker + padding).
6. Enters P/E mode (`flash_hp_init`), erases 1 block, writes back 64 bytes,
   exits P/E (`flash_hp_exit`).

Called immediately before `s_ctx.state = FWUPDATE_STATE_DONE`.

### Boot-time OTA Model Selection

`iaq_flash_model_valid(uint32_t *out_len)` in `iaq_predictor.cpp`:

- Reads bytes `[0..7]` from `DATA_FLASH_LAST_BLOCK_ADDR`.
- Returns `true` if magic == `0xDEADBEEF` and `model_len` is within bounds.
- `IAQ_Init()` uses `(const uint8_t *)DATA_FLASH_BASE` as the model pointer
  when valid, otherwise falls back to the compiled-in `g_iaq_model_data[]`.

> **Note**: bytes `[12..23]` are owned by `safe_reset.c` and must not be
> modified by the FWUpdate path.  `fwupdate_write_nvs_metadata()` preserves
> them via the read-modify-write sequence.