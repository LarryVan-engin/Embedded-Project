/*
 * fwupdate_receiver.c — UART frame receiver & Data Flash writer for RA6M5.
 *
 * Architecture:
 *   fwupdate_receiver_run() polls RX bytes from UART and pushes them into
 *   ring_buf[], then drains ring_buf[] through the protocol state machine.
 *
 * UART channel/baud are configurable via FWUPDATE_UART_CHANNEL and
 * FWUPDATE_UART_BAUDRATE (default: old debug UART channel/wiring).
 */

#include "fwupdate_receiver.h"
#include "drv_uart.h"
#include "drv_clk.h"
#include "drv_flash_hp.h"
#include "debug_print.h"   /* OTA debug logging */

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
 * Cortex-M33 system reset via SCB AIRCR register.
 * Matches the hw_system_reset() pattern in safe_reset.c.
 * ----------------------------------------------------------------------- */
#define FWUPDATE_SCB_AIRCR  (*(volatile uint32_t *)0xE000ED0CUL)
#define FWUPDATE_AIRCR_RESET  ((0x05FAUL << 16U) | (1UL << 2U))

static void fwupdate_system_reset(void)
{
    __asm volatile("dsb 0xF" ::: "memory");
    FWUPDATE_SCB_AIRCR = FWUPDATE_AIRCR_RESET;
    __asm volatile("dsb 0xF" ::: "memory");
    for (;;) { __asm volatile("nop"); }
}

/* -----------------------------------------------------------------------
 * Internal UART send helpers (blocking) — used only for ACK/NACK frames.
 * This avoids pulling in the entire drv_uart API for the TX path.
 * ----------------------------------------------------------------------- */
#define FWUPDATE_UART_CH   ((uint8_t)FWUPDATE_UART_CHANNEL)

static void uart2_send_byte(uint8_t b)
{
    UART_SendChar((UART_t)FWUPDATE_UART_CH, (char)b);
}

/* -----------------------------------------------------------------------
 * Static context — single instance (one active transfer at a time).
 * ----------------------------------------------------------------------- */
static fwupdate_ctx_t s_ctx;

/* -----------------------------------------------------------------------
 * crc16_ccitt — CRC-16-CCITT (XMODEM variant).
 *
 * Polynomial : 0x1021  (x^16 + x^12 + x^5 + 1)
 * Init value : 0xFFFF
 * No reflection of input or output bits.
 *
 * Algorithm: bit-by-bit with MSB-first shift, no lookup table.
 * Both sides (ESP32-C6 and RA6M5) MUST use identical constants.
 * ----------------------------------------------------------------------- */
uint16_t crc16_ccitt(uint8_t const * const p_data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    uint32_t idx;
    uint8_t  bit;

    if (p_data == NULL)
    {
        return 0U;
    }

    for (idx = 0U; idx < len; idx++)
    {
        /* XOR next byte into MSB of CRC */
        crc ^= (uint16_t)((uint16_t)p_data[idx] << 8U);

        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((uint16_t)(crc << 1U) ^ 0x1021U);
            }
            else
            {
                crc = (uint16_t)(crc << 1U);
            }
        }
    }

    return crc;
}

/* -----------------------------------------------------------------------
 * Internal: send an ACK frame.
 *   [STX][CMD_ACK][0x00][0x01][echo_cmd][CRC_MSB][CRC_LSB][ETX]
 * ----------------------------------------------------------------------- */
static void send_ack(uint8_t echo_cmd)
{
    uint8_t  crc_buf[4];
    uint16_t crc;

    crc_buf[0] = FWUPDATE_CMD_ACK;
    crc_buf[1] = 0x00U;
    crc_buf[2] = 0x01U;
    crc_buf[3] = echo_cmd;
    crc = crc16_ccitt(crc_buf, 4U);

    debug_print("[FWU] TX ACK echo_cmd=0x%02X crc=0x%04X\r\n",
                echo_cmd, (unsigned)crc);

    uart2_send_byte(FWUPDATE_STX);
    uart2_send_byte(FWUPDATE_CMD_ACK);
    uart2_send_byte(0x00U);                          /* LEN_MSB  */
    uart2_send_byte(0x01U);                          /* LEN_LSB  */
    uart2_send_byte(echo_cmd);                       /* DATA     */
    uart2_send_byte((uint8_t)((crc >> 8U) & 0xFFU));
    uart2_send_byte((uint8_t)(crc & 0xFFU));
    uart2_send_byte(FWUPDATE_ETX);
}

/* -----------------------------------------------------------------------
 * Internal: send a NACK frame.
 *   [STX][CMD_NACK][0x00][0x01][reason][CRC_MSB][CRC_LSB][ETX]
 * ----------------------------------------------------------------------- */
static void send_nack(uint8_t reason)
{
    uint8_t  crc_buf[4];
    uint16_t crc;

    crc_buf[0] = FWUPDATE_CMD_NACK;
    crc_buf[1] = 0x00U;
    crc_buf[2] = 0x01U;
    crc_buf[3] = reason;
    crc = crc16_ccitt(crc_buf, 4U);

    debug_print("[FWU] TX NACK reason=0x%02X crc=0x%04X\r\n",
                reason, (unsigned)crc);

    uart2_send_byte(FWUPDATE_STX);
    uart2_send_byte(FWUPDATE_CMD_NACK);
    uart2_send_byte(0x00U);
    uart2_send_byte(0x01U);
    uart2_send_byte(reason);
    uart2_send_byte((uint8_t)((crc >> 8U) & 0xFFU));
    uart2_send_byte((uint8_t)(crc & 0xFFU));
    uart2_send_byte(FWUPDATE_ETX);
}

/* -----------------------------------------------------------------------
 * Internal: validate received frame CRC.
 *
 * CRC is computed over: [CMD][LEN_MSB][LEN_LSB][DATA...]
 * Returns 1 if CRC matches, 0 otherwise.
 * ----------------------------------------------------------------------- */
static uint8_t frame_crc_valid(void)
{
    /* Build crc_input = [CMD, LEN_MSB, LEN_LSB, DATA...] */
    uint8_t  crc_buf[3U + FWUPDATE_MAX_DATA_LEN + 4U];
    uint16_t crc_len;
    uint16_t computed_crc;

    crc_buf[0] = s_ctx.frame_cmd;
    crc_buf[1] = (uint8_t)((s_ctx.frame_len >> 8U) & 0xFFU);
    crc_buf[2] = (uint8_t)(s_ctx.frame_len & 0xFFU);

    /* Copy data bytes into local buffer (MISRA: no pointer arithmetic on frame_data) */
    {
        uint16_t i;
        for (i = 0U; i < s_ctx.frame_len; i++)
        {
            crc_buf[3U + i] = s_ctx.frame_data[i];
        }
    }

    crc_len = (uint16_t)(3U + s_ctx.frame_len);
    computed_crc = crc16_ccitt(crc_buf, (uint32_t)crc_len);

    debug_print("[FWU] frame_crc_valid: cmd=0x%02X len=%u  computed=0x%04X  recv=0x%04X  %s\r\n",
                s_ctx.frame_cmd, s_ctx.frame_len,
                (unsigned)computed_crc, (unsigned)s_ctx.frame_crc_received,
                (computed_crc == s_ctx.frame_crc_received) ? "OK" : "MISMATCH");

    return (computed_crc == s_ctx.frame_crc_received) ? 1U : 0U;
}

/* -----------------------------------------------------------------------
 * Internal: write model validity metadata to the reserved NVS block.
 *
 * NVS block location: DATA_FLASH_LAST_BLOCK_ADDR (last 64-byte block)
 * Layout (64 bytes):
 *   [0..3]   Magic 0xDE 0xAD 0xBE 0xEF  — valid model indicator
 *   [4..7]   model_len (big-endian uint32_t)
 *   [8..9]   model_crc (big-endian uint16_t)
 *   [10..63] 0xFF padding
 *
 * After a successful transfer iaq_predictor.cpp reads this block on
 * startup. If the magic matches, it uses the Data Flash model at
 * DATA_FLASH_BASE instead of the compiled-in g_iaq_model_data[].
 * This persists across power cycles / resets.
 * ----------------------------------------------------------------------- */
static void fwupdate_write_nvs_metadata(uint32_t model_len, uint16_t model_crc)
{
    uint8_t           nvs_data[DATA_FLASH_BLOCK_SIZE];
    uint32_t          i;
    flash_hp_status_t st;

    /* Fill entire block with 0xFF (erased state) */
    for (i = 0UL; i < DATA_FLASH_BLOCK_SIZE; i++)
    {
        nvs_data[i] = 0xFFU;
    }

    /* Magic marker */
    nvs_data[0] = 0xDEU;
    nvs_data[1] = 0xADU;
    nvs_data[2] = 0xBEU;
    nvs_data[3] = 0xEFU;

    /* Model length (big-endian) */
    nvs_data[4] = (uint8_t)((model_len >> 24U) & 0xFFU);
    nvs_data[5] = (uint8_t)((model_len >> 16U) & 0xFFU);
    nvs_data[6] = (uint8_t)((model_len >>  8U) & 0xFFU);
    nvs_data[7] = (uint8_t)(model_len & 0xFFU);

    /* Model CRC-16 (big-endian) */
    nvs_data[8] = (uint8_t)((model_crc >> 8U) & 0xFFU);
    nvs_data[9] = (uint8_t)(model_crc & 0xFFU);

    /* Re-enter P/E mode for NVS block write */
    st = flash_hp_init();
    if (st != FLASH_HP_OK)
    {
        return;
    }

    /* Erase NVS block (1 × 64-byte block) */
    st = flash_hp_erase(DATA_FLASH_LAST_BLOCK_ADDR, 1U);
    if (st != FLASH_HP_OK)
    {
        flash_hp_exit();
        return;
    }

    /* Write NVS metadata (must be 4-byte aligned length = 64 bytes) */
    st = flash_hp_write(DATA_FLASH_LAST_BLOCK_ADDR, nvs_data, DATA_FLASH_BLOCK_SIZE);
    flash_hp_exit();
}

/* -----------------------------------------------------------------------
 * Internal: process a complete, CRC-verified frame.
 * Returns FLASH_HP_OK on success.
 * ----------------------------------------------------------------------- */
static flash_hp_status_t process_frame(void)
{
    flash_hp_status_t status = FLASH_HP_OK;

    switch (s_ctx.frame_cmd)
    {
        /* ----------------------------------------------------------------
         * CMD_START — begin a new transfer.
         * Payload: 4-byte big-endian total model length.
         * ---------------------------------------------------------------- */
        case FWUPDATE_CMD_START:
        {
            uint32_t total_len;
            uint32_t num_blocks;

            debug_print("[FWU] CMD_START received, frame_len=%u\r\n", s_ctx.frame_len);

            if (s_ctx.frame_len != 4U)
            {
                debug_print("[FWU] CMD_START bad payload len=%u (expected 4) -> NACK_SEQUENCE\r\n",
                            s_ctx.frame_len);
                send_nack(FWUPDATE_NACK_SEQUENCE);
                break;
            }

            /* Reconstruct total length from big-endian payload */
            total_len  = (uint32_t)s_ctx.frame_data[0] << 24U;
            total_len |= (uint32_t)s_ctx.frame_data[1] << 16U;
            total_len |= (uint32_t)s_ctx.frame_data[2] << 8U;
            total_len |= (uint32_t)s_ctx.frame_data[3];

            debug_print("[FWU] CMD_START total_len=%lu  MAX=%lu\r\n",
                        total_len, (uint32_t)FWUPDATE_MAX_MODEL_LEN);

            if (total_len == 0UL || total_len > FWUPDATE_MAX_MODEL_LEN)
            {
                debug_print("[FWU] CMD_START NACK_LENGTH: total_len=%lu out of range\r\n", total_len);
                send_nack(FWUPDATE_NACK_LENGTH);
                break;
            }

            s_ctx.model_total_len    = total_len;
            s_ctx.model_written_len  = 0UL;
            s_ctx.image_crc_running  = 0xFFFFU; /* init for CRC-CCITT */
            s_ctx.flash_write_addr   = FWUPDATE_FLASH_BASE;

            /* Initialise Flash HP P/E mode */
            debug_print("[FWU] flash_hp_init()...\r\n");
            status = flash_hp_init();
            if (status != FLASH_HP_OK)
            {
                debug_print("[FWU] flash_hp_init FAILED status=%d -> NACK_FLASH\r\n", (int)status);
                send_nack(FWUPDATE_NACK_FLASH);
                break;
            }

            /* Erase required blocks — ceil(total_len / 64) */
            num_blocks = (total_len + DATA_FLASH_BLOCK_SIZE - 1UL) / DATA_FLASH_BLOCK_SIZE;
            debug_print("[FWU] Erasing %lu blocks @ 0x%08lX...\r\n",
                        num_blocks, (uint32_t)FWUPDATE_FLASH_BASE);
            status = flash_hp_erase(FWUPDATE_FLASH_BASE, num_blocks);
            if (status != FLASH_HP_OK)
            {
                debug_print("[FWU] flash_hp_erase FAILED status=%d -> NACK_FLASH\r\n", (int)status);
                flash_hp_exit();
                send_nack(FWUPDATE_NACK_FLASH);
                break;
            }

            debug_print("[FWU] Erase OK -> sending ACK\r\n");
            s_ctx.state = FWUPDATE_STATE_IDLE;   /* ready for CMD_DATA frames */
            send_ack(FWUPDATE_CMD_START);
            break;
        }

        /* ----------------------------------------------------------------
         * CMD_DATA — write a data chunk to Data Flash.
         * Payload: 1..128 bytes of model data.
         * ---------------------------------------------------------------- */
        case FWUPDATE_CMD_DATA:
        {
            uint32_t bytes_remaining;
            uint32_t chunk_len;
            uint32_t padded_len;

            if (s_ctx.model_total_len == 0UL)
            {
                /* CMD_START not received yet */
                send_nack(FWUPDATE_NACK_SEQUENCE);
                break;
            }
            if (s_ctx.frame_len == 0U || s_ctx.frame_len > FWUPDATE_MAX_DATA_LEN)
            {
                send_nack(FWUPDATE_NACK_LENGTH);
                break;
            }

            if (s_ctx.model_written_len >= s_ctx.model_total_len)
            {
                send_nack(FWUPDATE_NACK_SEQUENCE);
                break;
            }

            chunk_len = (uint32_t)s_ctx.frame_len;
            bytes_remaining = s_ctx.model_total_len - s_ctx.model_written_len;

            if (chunk_len > bytes_remaining)
            {
                send_nack(FWUPDATE_NACK_LENGTH);
                break;
            }

            /* Update running image CRC over raw payload bytes */
            {
                uint16_t crc = s_ctx.image_crc_running;
                uint32_t i;
                uint8_t  bit;

                for (i = 0UL; i < chunk_len; i++)
                {
                    crc ^= (uint16_t)((uint16_t)s_ctx.frame_data[i] << 8U);
                    for (bit = 0U; bit < 8U; bit++)
                    {
                        if ((crc & 0x8000U) != 0U)
                        {
                            crc = (uint16_t)((uint16_t)(crc << 1U) ^ 0x1021U);
                        }
                        else
                        {
                            crc = (uint16_t)(crc << 1U);
                        }
                    }
                }
                s_ctx.image_crc_running = crc;
            }

            /* Data Flash write unit is 4 bytes on RA6M5 — pad tail bytes with 0xFF. */
            padded_len = chunk_len;
            if ((padded_len & (FWUPDATE_FLASH_WRITE_ALIGN - 1UL)) != 0UL)
            {
                uint32_t pad = FWUPDATE_FLASH_WRITE_ALIGN -
                               (padded_len & (FWUPDATE_FLASH_WRITE_ALIGN - 1UL));
                uint32_t p;
                for (p = 0UL; p < pad; p++)
                {
                    /* frame_data buffer is sized for this padding */
                    s_ctx.frame_data[padded_len + p] = 0xFFU;
                }
                padded_len += pad;
            }

            if ((s_ctx.flash_write_addr >= FWUPDATE_FLASH_END) ||
                (padded_len > (FWUPDATE_FLASH_END - s_ctx.flash_write_addr)))
            {
                send_nack(FWUPDATE_NACK_FLASH);
                break;
            }

            status = flash_hp_write(s_ctx.flash_write_addr,
                                    (uint8_t const *)s_ctx.frame_data,
                                    padded_len);
            if (status != FLASH_HP_OK)
            {
                flash_hp_exit();
                send_nack(FWUPDATE_NACK_FLASH);
                break;
            }

            s_ctx.model_written_len += chunk_len;
            s_ctx.flash_write_addr  += padded_len;

            send_ack(FWUPDATE_CMD_DATA);
            break;
        }

        /* ----------------------------------------------------------------
         * CMD_END — verify the complete image.
         * Payload: 2-byte big-endian full-image CRC.
         * ---------------------------------------------------------------- */
        case FWUPDATE_CMD_END:
        {
            uint16_t received_image_crc;

            if (s_ctx.frame_len != 2U)
            {
                send_nack(FWUPDATE_NACK_SEQUENCE);
                break;
            }

            received_image_crc  = (uint16_t)((uint16_t)s_ctx.frame_data[0] << 8U);
            received_image_crc |= (uint16_t)s_ctx.frame_data[1];

            if (s_ctx.model_written_len != s_ctx.model_total_len)
            {
                flash_hp_exit();
                send_nack(FWUPDATE_NACK_SEQUENCE);
                break;
            }

            /* Compare running image CRC */
            if (s_ctx.image_crc_running != received_image_crc)
            {
                flash_hp_exit();
                send_nack(FWUPDATE_NACK_IMAGE_CRC);
                break;
            }

            /* Exit P/E mode before read-back verify */
            flash_hp_exit();

            /* Read-back verify: compare Data Flash to the expected CRC.
             * Re-compute CRC directly from flash read-mapped region. */
            {
                /* Build a local CRC over the flash-mapped region */
                volatile uint8_t const * const p_flash =
                    (volatile uint8_t const *)(uintptr_t)FWUPDATE_FLASH_BASE;
                uint16_t verify_crc = 0xFFFFU;
                uint32_t i;
                uint8_t  bit;

                for (i = 0UL; i < s_ctx.model_written_len; i++)
                {
                    verify_crc ^= (uint16_t)((uint16_t)p_flash[i] << 8U);
                    for (bit = 0U; bit < 8U; bit++)
                    {
                        if ((verify_crc & 0x8000U) != 0U)
                        {
                            verify_crc = (uint16_t)((uint16_t)(verify_crc << 1U) ^ 0x1021U);
                        }
                        else
                        {
                            verify_crc = (uint16_t)(verify_crc << 1U);
                        }
                    }
                }

                if (verify_crc != received_image_crc)
                {
                    send_nack(FWUPDATE_NACK_VERIFY);
                    break;
                }
            }

            /* All checks passed — persist metadata so RA6M5 boots from Data Flash */
            fwupdate_write_nvs_metadata(s_ctx.model_written_len, received_image_crc);

            s_ctx.state = FWUPDATE_STATE_DONE;
            send_ack(FWUPDATE_CMD_END);

            /* Small delay to let the ACK frame fully transmit before reset */
            {
                volatile uint32_t i;
                for (i = 0UL; i < 2000000UL; i++) { __asm volatile("nop"); }
            }

            /* Reset MCU — on next boot iaq_predictor will load the new model
             * from Data Flash (NVS magic marker will be valid). */
            debug_print("[FWU] OTA complete! Resetting MCU...\r\n");
            fwupdate_system_reset();
            break;
        }

        default:
        {
            send_nack(FWUPDATE_NACK_SEQUENCE);
            break;
        }
    }

    return status;
}

/* -----------------------------------------------------------------------
 * fwupdate_receiver_init
 * ----------------------------------------------------------------------- */
void fwupdate_receiver_init(void)
{
    uint32_t i;

    /* Zero-initialise context */
    s_ctx.ring_head           = 0U;
    s_ctx.ring_tail           = 0U;
    s_ctx.state               = FWUPDATE_STATE_IDLE;
    s_ctx.frame_cmd           = 0U;
    s_ctx.frame_len           = 0U;
    s_ctx.frame_data_idx      = 0U;
    s_ctx.frame_crc_received  = 0U;
    s_ctx.model_total_len     = 0UL;
    s_ctx.model_written_len   = 0UL;
    s_ctx.image_crc_running   = 0xFFFFU;
    s_ctx.flash_write_addr    = FWUPDATE_FLASH_BASE;

    for (i = 0U; i < FWUPDATE_RING_BUF_SIZE; i++)
    {
        s_ctx.ring_buf[i] = 0U;
    }
    for (i = 0U; i < (FWUPDATE_MAX_DATA_LEN + 4U); i++)
    {
        s_ctx.frame_data[i] = 0U;
    }

    /* NOTE: UART_Init() is intentionally NOT called here.
     * server_comm_uart_init() already initialised this SCI channel before
     * fwupdate_receiver_init() is called (see server_comm_init() call order).
     * Calling UART_Init() a second time resets SCR (TE|RE → 0 then back to 1)
     * which causes the first few bytes of any incoming CMD_START frame to be
     * dropped, resulting in a permanent CMD_START failed condition.
     *
     * If you ever call fwupdate_receiver_init() standalone (without
     * server_comm_uart_init() running first), uncomment the line below:
     */
    /* UART_Init((UART_t)FWUPDATE_UART_CH, FWUPDATE_UART_BAUDRATE); */
}

/* -----------------------------------------------------------------------
 * fwupdate_receiver_run — call from main loop.
 * ----------------------------------------------------------------------- */
fwupdate_state_t fwupdate_receiver_run(void)
{
    while ((SCI_SSR(FWUPDATE_UART_CH) & SSR_RDRF) != 0U)
    {
        uint16_t next_head;
        uint8_t byte_in = SCI_RDR(FWUPDATE_UART_CH);

        next_head = (uint16_t)((s_ctx.ring_head + 1U) % FWUPDATE_RING_BUF_SIZE);
        if (next_head != s_ctx.ring_tail)
        {
            s_ctx.ring_buf[s_ctx.ring_head] = byte_in;
            s_ctx.ring_head                 = next_head;
        }
    }

    /* Drain the ring buffer one byte at a time */
    while (s_ctx.ring_tail != s_ctx.ring_head)
    {
        uint8_t b = s_ctx.ring_buf[s_ctx.ring_tail];
        s_ctx.ring_tail = (uint16_t)((s_ctx.ring_tail + 1U) % FWUPDATE_RING_BUF_SIZE);

        /* Feed byte into frame state machine */
        switch (s_ctx.state)
        {
            case FWUPDATE_STATE_IDLE:
                if (b == FWUPDATE_STX)
                {
                    /* Reset frame context for new frame */
                    s_ctx.frame_cmd      = 0U;
                    s_ctx.frame_len      = 0U;
                    s_ctx.frame_data_idx = 0U;
                    s_ctx.state          = FWUPDATE_STATE_CMD;
                }
                break;

            case FWUPDATE_STATE_CMD:
                s_ctx.frame_cmd = b;
                s_ctx.state     = FWUPDATE_STATE_LEN_MSB;
                break;

            case FWUPDATE_STATE_LEN_MSB:
                s_ctx.frame_len = (uint16_t)((uint16_t)b << 8U);
                s_ctx.state     = FWUPDATE_STATE_LEN_LSB;
                break;

            case FWUPDATE_STATE_LEN_LSB:
                s_ctx.frame_len |= (uint16_t)b;
                s_ctx.frame_data_idx = 0U;

                if (s_ctx.frame_len == 0U)
                {
                    /* No data — go straight to CRC */
                    s_ctx.state = FWUPDATE_STATE_CRC_MSB;
                }
                else if (s_ctx.frame_len > (FWUPDATE_MAX_DATA_LEN + 4U))
                {
                    /* Oversized — abort and wait for next STX */
                    s_ctx.state = FWUPDATE_STATE_IDLE;
                }
                else
                {
                    s_ctx.state = FWUPDATE_STATE_DATA;
                }
                break;

            case FWUPDATE_STATE_DATA:
                s_ctx.frame_data[s_ctx.frame_data_idx] = b;
                s_ctx.frame_data_idx++;

                if (s_ctx.frame_data_idx >= s_ctx.frame_len)
                {
                    s_ctx.state = FWUPDATE_STATE_CRC_MSB;
                }
                break;

            case FWUPDATE_STATE_CRC_MSB:
                s_ctx.frame_crc_received = (uint16_t)((uint16_t)b << 8U);
                s_ctx.state = FWUPDATE_STATE_CRC_LSB;
                break;

            case FWUPDATE_STATE_CRC_LSB:
                s_ctx.frame_crc_received |= (uint16_t)b;
                s_ctx.state = FWUPDATE_STATE_ETX;
                break;

            case FWUPDATE_STATE_ETX:
                if (b == FWUPDATE_ETX)
                {
                    /* Validate CRC */
                    if (frame_crc_valid() != 0U)
                    {
                        /* Process the valid frame */
                        (void)process_frame();
                    }
                    else
                    {
                        send_nack(FWUPDATE_NACK_CRC);
                    }
                }
                /* Return to idle regardless — wait for next STX */
                if (s_ctx.state != FWUPDATE_STATE_DONE)
                {
                    s_ctx.state = FWUPDATE_STATE_IDLE;
                }
                break;

            case FWUPDATE_STATE_PROCESS:
                break;

            case FWUPDATE_STATE_DONE:
                if (b == FWUPDATE_STX)
                {
                    s_ctx.frame_cmd      = 0U;
                    s_ctx.frame_len      = 0U;
                    s_ctx.frame_data_idx = 0U;
                    s_ctx.state          = FWUPDATE_STATE_CMD;
                }
                break;

            default:
                /* No action — absorb bytes */
                break;
        }
    }

    return s_ctx.state;
}
