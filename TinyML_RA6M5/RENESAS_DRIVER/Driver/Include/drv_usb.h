#ifndef DRV_USB_H
#define DRV_USB_H

#include <stdint.h>
#include "drv_common.h"

/*
 * Device CDC host-ready policy:
 *   0: host-ready when USB is configured (lenient, no DTR required)
 *   1: host-ready only after SET_CONTROL_LINE_STATE asserts DTR/RTS
 */
#ifndef USB_DEV_CDC_REQUIRE_DTR
#define USB_DEV_CDC_REQUIRE_DTR 0U
#endif

/*
 * drv_usb.h — RA6M5 USBFS bare-metal dual-mode driver.
 *
 * Supported modes:
 *   1) Device CDC (logging over BULK IN)
 *   2) Host CDC-ACM (AT command transport over BULK OUT/BULK IN)
 *
 * This module accesses USBFS registers directly and does not use FSP USB APIs.
 */

typedef enum
{
    USB_MODE_DEVICE_CDC = 0,
    USB_MODE_HOST_CDC_ACM = 1
} usb_mode_t;

typedef struct
{
    uint32_t baudrate;
    uint8_t stop_bits;
    uint8_t parity;
    uint8_t data_bits;
} usb_line_coding_t;

typedef struct
{
    uint8_t address;
    uint8_t config_value;
    uint8_t bulk_in_ep;
    uint8_t bulk_out_ep;
    uint16_t bulk_in_mps;
    uint16_t bulk_out_mps;
    uint8_t configured;
} usb_host_cdc_device_t;

typedef struct
{
    uint8_t device_desc[18];
    uint8_t max_packet_size_ep0;
    uint16_t config_total_length;
    uint8_t num_interfaces;
    uint8_t configuration_value;
} usb_host_descriptor_snapshot_t;

typedef enum
{
    USB_DBG_EVT_INIT = 1,
    USB_DBG_EVT_DVST = 2,
    USB_DBG_EVT_CTRT = 3,
    USB_DBG_EVT_SETUP = 4,
    USB_DBG_EVT_STATE = 5,
    USB_DBG_EVT_LINE_CODING = 6,
    USB_DBG_EVT_STALL = 7,
    USB_DBG_EVT_ERROR = 8
} usb_debug_event_t;

typedef enum
{
    USB_DBG_STATE_GET_DESCRIPTOR = 1,
    USB_DBG_STATE_SET_ADDRESS = 2,
    USB_DBG_STATE_SET_CONFIGURATION = 3,
    USB_DBG_STATE_SET_INTERFACE = 4,
    USB_DBG_STATE_SET_LINE_CODING_PENDING = 5,
    USB_DBG_STATE_GET_LINE_CODING = 6,
    USB_DBG_STATE_SET_CONTROL_LINE_STATE = 7,
    USB_DBG_STATE_SEND_BREAK = 8,
    USB_DBG_STATE_SW_CONFIGURED = 9,
    USB_DBG_STATE_STATUS_STAGE = 10,
    USB_DBG_STATE_PULLUP_ENABLED = 11,
    USB_DBG_STATE_BUS_RESET_REARM = 12,
    USB_DBG_STATE_PULLUP_DISABLED = 13,
    USB_DBG_STATE_CTRL_IN_PACKET = 14
} usb_debug_state_t;

typedef enum
{
    USB_DBG_STALL_UNSUPPORTED_STANDARD = 1,
    USB_DBG_STALL_UNSUPPORTED_CLASS = 2,
    USB_DBG_STALL_UNSUPPORTED_TYPE = 3,
    USB_DBG_STALL_CONTROL_SEQUENCE = 4,
    USB_DBG_STALL_SET_LINE_CODING_DATA = 5
} usb_debug_stall_reason_t;

typedef enum
{
    USB_DBG_ERROR_SET_LINE_CODING_READ = 1
} usb_debug_error_t;

typedef struct
{
    uint8_t event;
    uint16_t param0;
    uint16_t param1;
    uint16_t param2;
    uint16_t param3;
} usb_debug_trace_t;

/* -----------------------------------------------------------------------
 * Driver lifecycle
 * ----------------------------------------------------------------------- */
drv_status_t USB_Init(usb_mode_t mode);
void USB_Deinit(void);

/* -----------------------------------------------------------------------
 * Event handling
 * ----------------------------------------------------------------------- */
void USB_PollEvents(void);
void USBI0_IRQHandler(void);
void USBI1_IRQHandler(void);
uint8_t USB_Debug_PopTrace(usb_debug_trace_t *trace);
uint16_t USB_Debug_GetDroppedTraceCount(void);

/* -----------------------------------------------------------------------
 * Device CDC API (logging)
 * ----------------------------------------------------------------------- */
drv_status_t USB_Dev_Write(const uint8_t *data, uint32_t len);
drv_status_t USB_Dev_Printf(const char *fmt, ...);

void USB_Dev_SetConfigured(uint8_t configured);
uint8_t USB_Dev_IsConfigured(void);
uint8_t USB_Dev_IsHostReady(void);

/* -----------------------------------------------------------------------
 * Host CDC-ACM API (SIM module)
 * ----------------------------------------------------------------------- */
drv_status_t USB_Host_Enumerate(void);
drv_status_t USB_Host_Send_AT_Command(char *cmd);
int32_t USB_Host_Read_Response(uint8_t *buffer, uint32_t len);
const usb_host_cdc_device_t *USB_Host_GetDeviceInfo(void);
const usb_host_descriptor_snapshot_t *USB_Host_GetDescriptorSnapshot(void);

#endif /* DRV_USB_H */
