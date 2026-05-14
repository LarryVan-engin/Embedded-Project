#ifndef FWUPDATE_RECEIVER_H
#define FWUPDATE_RECEIVER_H

/*
 * fwupdate_receiver.h — UART frame receiver & Data Flash writer for RA6M5.
 *
 * Protocol summary (see imple_doc/FW_FWUpdate_Receiver.md):
 *
 *   Frame format:
 *     [STX 0x02][CMD 1B][LEN_MSB][LEN_LSB][DATA 0..N][CRC_MSB][CRC_LSB][ETX 0x03]
 *
 *   CRC scope: CMD + LEN_MSB + LEN_LSB + DATA (everything between STX and CRC).
 *   CRC algorithm: CRC-16-CCITT, poly=0x1021, init=0xFFFF, no reflect (XMODEM).
 *
 *   Commands (inbound from ESP32-C6):
 *     0x01 CMD_START  — payload: 4-byte uint32_t total model length (big-endian)
 *     0x02 CMD_DATA   — payload: up to 128 bytes of model data
 *     0x03 CMD_END    — payload: 2-byte uint16_t full-image CRC (big-endian)
 *
 *   Responses (outbound to ESP32-C6):
 *     0xAA CMD_ACK    — frame accepted (1-byte payload: echo of CMD)
 *     0xFF CMD_NACK   — error (1-byte payload: error code)
 *
 * UART channel used: same channel as legacy debug UART (configurable via
 * FWUPDATE_UART_CHANNEL, default OS_DEBUG_UART_CHANNEL).
 * Baud rate: FWUPDATE_UART_BAUDRATE (default OS_DEBUG_UART_BAUDRATE).
 *
 * RX path is polled from fwupdate_receiver_run() for easy reuse of the old
 * debug wiring without extra NVIC/vector coupling.
 *
 * MISRA-C:2012 compliance: stdint.h types, bounds checks, no dynamic allocation.
 */

#include <stdint.h>
#include "drv_flash_hp.h"
#include "rtos_config.h"

#ifndef FWUPDATE_UART_CHANNEL
  #ifdef SERVER_COMM_UART_CHANNEL
    #define FWUPDATE_UART_CHANNEL  SERVER_COMM_UART_CHANNEL   /* board-specific comm UART */
  #else
    #define FWUPDATE_UART_CHANNEL  OS_DEBUG_UART_CHANNEL      /* fallback: shared debug UART */
  #endif
#endif

#ifndef FWUPDATE_UART_BAUDRATE
  #ifdef SERVER_COMM_UART_BAUDRATE
    #define FWUPDATE_UART_BAUDRATE SERVER_COMM_UART_BAUDRATE
  #else
    #define FWUPDATE_UART_BAUDRATE OS_DEBUG_UART_BAUDRATE
  #endif
#endif

/* -----------------------------------------------------------------------
 * Protocol constants
 * ----------------------------------------------------------------------- */
#define FWUPDATE_STX           0x02U   /* Frame start byte                 */
#define FWUPDATE_ETX           0x03U   /* Frame end byte                   */

#define FWUPDATE_CMD_START     0x01U   /* Begin transfer                   */
#define FWUPDATE_CMD_DATA      0x02U   /* Data chunk (≤128 bytes)          */
#define FWUPDATE_CMD_END       0x03U   /* End & full-image CRC             */

#define FWUPDATE_CMD_ACK       0xAAU   /* Acknowledge — frame OK           */
#define FWUPDATE_CMD_NACK      0xFFU   /* Negative-ACK — retry             */

/* NACK reason codes (1-byte payload of a NACK frame) */
#define FWUPDATE_NACK_CRC       0x01U  /* Frame CRC mismatch              */
#define FWUPDATE_NACK_SEQUENCE  0x02U  /* Unexpected command               */
#define FWUPDATE_NACK_FLASH     0x03U  /* Flash erase/write error          */
#define FWUPDATE_NACK_LENGTH    0x04U  /* Declared length exceeds capacity */
#define FWUPDATE_NACK_VERIFY    0x05U  /* Read-back verify failed          */
#define FWUPDATE_NACK_IMAGE_CRC 0x06U  /* Full-image CRC mismatch          */

/* Maximum data bytes per CMD_DATA frame */
#define FWUPDATE_MAX_DATA_LEN  128U

/*
 * Reserve the final 64-byte Data Flash block for the RTOS NVS scratch test.
 * FWUpdate therefore owns the first 8128 bytes starting at 0x08000000.
 */
#define FWUPDATE_FLASH_BASE         DATA_FLASH_BASE
#define FWUPDATE_FLASH_SIZE         (DATA_FLASH_SIZE - DATA_FLASH_BLOCK_SIZE)
#define FWUPDATE_FLASH_END          (FWUPDATE_FLASH_BASE + FWUPDATE_FLASH_SIZE)
#define FWUPDATE_FLASH_WRITE_ALIGN  DATA_FLASH_WRITE_WORD_SIZE
#define FWUPDATE_MAX_MODEL_LEN      FWUPDATE_FLASH_SIZE

/* Internal receive ring buffer size — large enough for one full frame */
#define FWUPDATE_RING_BUF_SIZE 256U

/* -----------------------------------------------------------------------
 * Receiver state machine states
 * ----------------------------------------------------------------------- */
typedef enum {
    FWUPDATE_STATE_IDLE        = 0,   /* Waiting for STX                  */
    FWUPDATE_STATE_CMD         = 1,   /* Received STX, waiting for CMD    */
    FWUPDATE_STATE_LEN_MSB     = 2,   /* Waiting for LEN MSB              */
    FWUPDATE_STATE_LEN_LSB     = 3,   /* Waiting for LEN LSB              */
    FWUPDATE_STATE_DATA        = 4,   /* Collecting DATA bytes            */
    FWUPDATE_STATE_CRC_MSB     = 5,   /* Waiting for CRC MSB              */
    FWUPDATE_STATE_CRC_LSB     = 6,   /* Waiting for CRC LSB              */
    FWUPDATE_STATE_ETX         = 7,   /* Waiting for ETX                  */
    FWUPDATE_STATE_PROCESS     = 8,   /* Frame complete, ready to process */
    FWUPDATE_STATE_DONE        = 9    /* Transfer complete and verified   */
} fwupdate_state_t;

/* -----------------------------------------------------------------------
 * Transfer context — holds all state for an active firmware update.
 * ring_head is volatile because RX polling feeds bytes asynchronously.
 * ----------------------------------------------------------------------- */
typedef struct {
    /* Ring buffer (ISR writes, main loop reads) */
    volatile uint8_t ring_buf[FWUPDATE_RING_BUF_SIZE];
    volatile uint16_t ring_head;  /* ISR advances head        */
    uint16_t          ring_tail;  /* Main loop advances tail  */

    /* Frame parser state */
    fwupdate_state_t state;
    uint8_t  frame_cmd;
    uint16_t frame_len;
    uint16_t frame_data_idx;
    uint8_t  frame_data[FWUPDATE_MAX_DATA_LEN + 4U];  /* +4 for CMD_START payload */
    uint16_t frame_crc_received;

    /* Transfer progress */
    uint32_t model_total_len;    /* declared total length (from CMD_START) */
    uint32_t model_written_len;  /* bytes written to flash so far          */
    uint16_t image_crc_running;  /* running CRC over all DATA payload bytes */

    /* Flash write cursor */
    uint32_t flash_write_addr;
} fwupdate_ctx_t;

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/*
 * fwupdate_receiver_init — Initialise the firmware update receiver.
 *
 * Initialises the configured UART channel at FWUPDATE_UART_BAUDRATE.
 * Resets the context to idle state.
 *
 * Must be called once before fwupdate_receiver_run().
 */
void fwupdate_receiver_init(void);

/*
 * fwupdate_receiver_run — Main-loop polling function.
 *
 * Drains the ring buffer and feeds bytes through the frame state machine.
 * When a complete frame is received it processes it (erase/write/verify).
 * Sends ACK or NACK responses via FWUPDATE_UART_CHANNEL.
 *
 * Call this repeatedly from main() or a scheduler task.
 *
 * Returns: current receiver state (FWUPDATE_STATE_DONE when complete).
 */
fwupdate_state_t fwupdate_receiver_run(void);

/*
 * crc16_ccitt — CRC-16-CCITT (XMODEM variant).
 *
 * Polynomial : 0x1021
 * Init value : 0xFFFF
 * No input/output reflection.
 *
 * Both ESP32-C6 and RA6M5 use the identical algorithm — this function
 * is also declared in this header so unit tests can call it directly.
 *
 * Parameters:
 *   p_data : pointer to data buffer (must not be NULL)
 *   len    : number of bytes
 * Returns: 16-bit CRC.
 */
uint16_t crc16_ccitt(uint8_t const * const p_data, uint32_t len);

#endif /* FWUPDATE_RECEIVER_H */
