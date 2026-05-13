#include "drv_usb.h"

#include "GPIO.h"
#include "drv_clk.h"
#include "drv_rwp.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * RA6M5 USBFS register map (USBFS0)
 *
 * NOTE:
 * The project does not currently include a device-specific register header.
 * Keep this map centralized so it can be swapped with re_ra6m5.h aliases
 * without touching logic code.
 * ----------------------------------------------------------------------- */
#define USBFS0_BASE   0x40090000UL

#define USB8(off)     (*(volatile uint8_t *)(uintptr_t)(USBFS0_BASE + (uint32_t)(off)))
#define USB16(off)    (*(volatile uint16_t *)(uintptr_t)(USBFS0_BASE + (uint32_t)(off)))
#define USB32(off)    (*(volatile uint32_t *)(uintptr_t)(USBFS0_BASE + (uint32_t)(off)))

#define USB_SYSCFG     USB16(0x0000U)
#define USB_BUSWAIT    USB16(0x0002U)
#define USB_SYSSTS0    USB16(0x0004U)
#define USB_DVSTCTR0   USB16(0x0008U)

#define USB_CFIFO      USB32(0x0014U)
#define USB_CFIFO8     USB8(0x0014U)
#define USB_CFIFO16    USB16(0x0014U)
#define USB_D0FIFO     USB32(0x0018U)
#define USB_D1FIFO     USB32(0x001CU)

#define USB_CFIFOSEL   USB16(0x0020U)
#define USB_CFIFOCTR   USB16(0x0022U)
#define USB_D0FIFOSEL  USB16(0x0028U)
#define USB_D0FIFOCTR  USB16(0x002AU)
#define USB_D1FIFOSEL  USB16(0x002CU)
#define USB_D1FIFOCTR  USB16(0x002EU)

#define USB_INTENB0    USB16(0x0030U)
#define USB_INTENB1    USB16(0x0032U)
#define USB_BRDYENB    USB16(0x0036U)
#define USB_NRDYENB    USB16(0x0038U)
#define USB_BEMPENB    USB16(0x003AU)

#define USB_INTSTS0    USB16(0x0040U)
#define USB_INTSTS1    USB16(0x0042U)
#define USB_BRDYSTS    USB16(0x0046U)
#define USB_NRDYSTS    USB16(0x0048U)
#define USB_BEMPSTS    USB16(0x004AU)

#define USB_USBADDR    USB16(0x0050U)
#define USB_USBREQ     USB16(0x0054U)
#define USB_USBVAL     USB16(0x0056U)
#define USB_USBINDX    USB16(0x0058U)
#define USB_USBLENG    USB16(0x005AU)
#define USB_DCPCFG     USB16(0x005CU)
#define USB_DCPMAXP    USB16(0x005EU)
#define USB_DCPCTR     USB16(0x0060U)

#define USB_DPUSR0R_FS USB32(0x0400U)

#define USB_PIPESEL    USB16(0x0064U)
#define USB_PIPECFG    USB16(0x0068U)
#define USB_PIPEBUF    USB16(0x006AU)
#define USB_PIPEMAXP   USB16(0x006CU)
#define USB_PIPEPERI   USB16(0x006EU)

#define USB_PIPECTR(n) USB16((uint32_t)(0x0070U + (((uint32_t)(n) - 1U) * 2U)))

/* -----------------------------------------------------------------------
 * USBFS bit fields
 * ----------------------------------------------------------------------- */
#define USB_SYSCFG_USBE      (1U << 0)
#define USB_SYSCFG_DPRPU     (1U << 4)
#define USB_SYSCFG_DRPD      (1U << 5)
#define USB_SYSCFG_DCFM      (1U << 6)
#define USB_SYSCFG_SCKE      (1U << 10)

#define USB_SYSSTS0_LNST_MASK   (0x0003U)
#define USB_SYSSTS0_LNST_J       (0x0001U)
#define USB_SYSSTS0_LNST_K       (0x0002U)

#define USB_DVSTCTR0_UACT     (1U << 4)
#define USB_DVSTCTR0_USBRST    (1U << 6)

#define USB_DPUSR0R_FS_FIXPHY0 (1UL << 4)

#define USB_INT_VBINT          0x8000U
#define USB_INT_RESM           0x4000U
#define USB_INT_DVST           0x1000U
#define USB_INT_CTRT           0x0800U
#define USB_INT_BEMP           0x0400U
#define USB_INT_NRDY           0x0200U
#define USB_INT_BRDY           0x0100U
#define USB_INT_VBSTS          0x0080U
#define USB_INT_VALID          0x0008U
#define USB_INT_CTSQ_MASK      0x0007U
#define USB_INT_DVSQ_MASK      0x0070U

#define USB_DS_POWR            0x0000U
#define USB_DS_DFLT            0x0010U
#define USB_DS_ADDS            0x0020U
#define USB_DS_CNFG            0x0030U
#define USB_DS_SPD_POWR        0x0040U
#define USB_DS_SPD_DFLT        0x0050U
#define USB_DS_SPD_ADDR        0x0060U
#define USB_DS_SPD_CNFG        0x0070U

#define USB_CTR_BSTS           (1U << 15)
#define USB_CTR_SUREQ          (1U << 14)
#define USB_CTR_SQCLR          (1U << 8)
#define USB_CTR_SQSET          (1U << 7)
#define USB_CTR_ACLRM          (1U << 9)
#define USB_CTR_CCPL           0x0004U
#define USB_CTR_PID_MASK       0x0003U
#define USB_CTR_PID_STALL      0x0002U
#define USB_CTR_PID_BUF        0x0001U
#define USB_CTR_PID_NAK        0x0000U

#define USB_CS_IDST            0x0000U
#define USB_CS_RDDS            0x0001U
#define USB_CS_RDSS            0x0002U
#define USB_CS_WRDS            0x0003U
#define USB_CS_WRSS            0x0004U
#define USB_CS_WRND            0x0005U
#define USB_CS_SQER            0x0006U

#define USB_FIFOCTR_FRDY       (1U << 13)
#define USB_FIFOCTR_BVAL       (1U << 15)
#define USB_FIFOCTR_BCLR       (1U << 14)
#define USB_FIFOCTR_DTLN_MASK  (0x01FFU)

#define USB_BUSWAIT_5          0x0F05U
#define USB_FIFOSEL_MBW_MASK   0x0C00U
#define USB_FIFOSEL_MBW_16     0x0400U
#define USB_FIFOSEL_MBW_8      0x0000U

#define USB_CFIFOSEL_ISEL      (1U << 5)
#define USB_CFIFOSEL_CURPIPE_MASK (0x000FU)

#define USB_PIPECFG_TYPE_MASK      (0x0003U << 14)
#define USB_PIPECFG_INT            (0x0002U << 14)
#define USB_PIPECFG_BULK           (0x0001U << 14)
#define USB_PIPECFG_DIR            (1U << 4)
#define USB_PIPECFG_DBLB           (1U << 9)

#define USB_DIR_OUT 0U
#define USB_DIR_IN  1U

/* RequestType helpers */
#define USB_REQ_DIR_IN          0x80U
#define USB_REQ_TYPE_STANDARD   0x00U
#define USB_REQ_TYPE_CLASS      0x20U
#define USB_REQ_RECIP_DEVICE    0x00U
#define USB_REQ_RECIP_INTERFACE 0x01U

/* Standard requests */
#define USB_REQ_GET_STATUS        0x00U
#define USB_REQ_GET_DESCRIPTOR   0x06U
#define USB_REQ_GET_CONFIGURATION 0x08U
#define USB_REQ_SET_ADDRESS      0x05U
#define USB_REQ_SET_CONFIGURATION 0x09U
#define USB_REQ_GET_INTERFACE     0x0AU
#define USB_REQ_SET_INTERFACE     0x0BU

/* CDC ACM class requests */
#define USB_CDC_REQ_SET_LINE_CODING  0x20U
#define USB_CDC_REQ_GET_LINE_CODING  0x21U
#define USB_CDC_REQ_SET_CONTROL_LINE_STATE 0x22U
#define USB_CDC_REQ_SEND_BREAK       0x23U

/* Keep device-mode CDC TX failure bounded well below one second so the
 * polling task can observe close/detach events promptly. */
#define USB_CDC_DEV_TX_TIMEOUT_TICKS 1000000UL
#define USB_CDC_DEV_TX_POLL_STRIDE   1024UL

/* Descriptor types */
#define USB_DESC_DEVICE        0x01U
#define USB_DESC_CONFIGURATION 0x02U
#define USB_DESC_STRING        0x03U

/* USB setup packet */
typedef struct
{
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_packet_t;

typedef enum
{
    USB_DEV_PENDING_NONE = 0,
    USB_DEV_PENDING_SET_LINE_CODING = 1
} usb_dev_pending_request_t;

#define USB_DEBUG_TRACE_DEPTH  48U

/* -----------------------------------------------------------------------
 * Driver state
 * ----------------------------------------------------------------------- */
static usb_mode_t g_mode = USB_MODE_DEVICE_CDC;
static uint8_t g_usb_dev_address = 0U;
static uint8_t g_usb_dev_configured = 0U;
static uint8_t g_usb_dev_host_ready = 0U;
static usb_dev_pending_request_t g_dev_pending_request = USB_DEV_PENDING_NONE;
static uint8_t g_usb_interface_alt[2] = {0U, 0U};
static uint16_t g_usb_control_line_state = 0U;
static uint16_t g_usb_send_break_duration = 0U;
static const uint8_t *g_usb_ctrl_in_data = NULL;
static uint16_t g_usb_ctrl_in_remaining = 0U;
static uint8_t g_usb_device_pullup_enabled = 0U;
static uint8_t g_usb_device_attach_debounce = 0U;
static volatile uint8_t g_usb_poll_busy = 0U;
static uint8_t          g_usb_vbus_absent_count = 0U;
static uint16_t         g_usb_enum_suspend_count = 0U;
static usb_debug_trace_t g_usb_debug_trace[USB_DEBUG_TRACE_DEPTH];
static volatile uint8_t g_usb_debug_trace_head = 0U;
static volatile uint8_t g_usb_debug_trace_tail = 0U;
static volatile uint16_t g_usb_debug_trace_dropped = 0U;
static uint16_t g_usb_debug_last_setup_request = 0U;

static usb_line_coding_t g_line_coding = {
    115200UL, 0U, 0U, 8U
};

static usb_host_cdc_device_t g_host_dev;
static usb_host_descriptor_snapshot_t g_host_desc;

static void usb_clear_bemp(uint8_t pipe);
static void usb_device_abort_bulk_in(uint8_t pipe);
static drv_status_t usb_device_wait_bulk_in_complete(uint8_t pipe);

static void usb_debug_trace_reset(void)
{
    g_usb_debug_trace_head = 0U;
    g_usb_debug_trace_tail = 0U;
    g_usb_debug_trace_dropped = 0U;
    g_usb_debug_last_setup_request = 0U;
    memset(g_usb_debug_trace, 0, sizeof(g_usb_debug_trace));
}

static void usb_debug_trace_push(uint8_t event,
                                 uint16_t param0,
                                 uint16_t param1,
                                 uint16_t param2,
                                 uint16_t param3)
{
    uint8_t head = g_usb_debug_trace_head;
    uint8_t next = (uint8_t)((head + 1U) % USB_DEBUG_TRACE_DEPTH);

    if (next == g_usb_debug_trace_tail)
    {
        g_usb_debug_trace_dropped++;
        return;
    }

    g_usb_debug_trace[head].event = event;
    g_usb_debug_trace[head].param0 = param0;
    g_usb_debug_trace[head].param1 = param1;
    g_usb_debug_trace[head].param2 = param2;
    g_usb_debug_trace[head].param3 = param3;
    g_usb_debug_trace_head = next;
}

static uint16_t usb_debug_pack_request(const usb_setup_packet_t *setup)
{
    return (uint16_t)(((uint16_t)setup->bRequest << 8) | setup->bmRequestType);
}

/* EK-RA6M5 USBFS VBUSEN pin from configuration.xml: P500 */
#define USB_VBUS_EN_PORT GPIO_PORT5
#define USB_VBUS_EN_PIN  0U

/* MSTPCRB bit for USBFS module stop release (project-level constant). */
#define USBFS_MSTP_BIT   11U

/* Endpoint/pipe allocation for this driver */
#define USB_PIPE_CDC_DEV_BULK_IN   1U
#define USB_PIPE_CDC_DEV_BULK_OUT  2U
#define USB_PIPE_CDC_DEV_INT_IN    6U
#define USB_PIPE_CDC_HOST_BULK_OUT 1U
#define USB_PIPE_CDC_HOST_BULK_IN  2U

#define USB_CDC_CONFIG_VALUE       1U
#define USB_CDC_COMM_INTERFACE     0U
#define USB_CDC_DATA_INTERFACE     1U

#define USB_CDC_BULK_IN_EP_ADDR    0x81U
#define USB_CDC_BULK_OUT_EP_ADDR   0x02U
#define USB_CDC_INT_IN_EP_ADDR     0x83U

#define USB_CDC_BULK_IN_EP_NUM     1U
#define USB_CDC_BULK_OUT_EP_NUM    2U
#define USB_CDC_INT_IN_EP_NUM      3U

#define USB_PIPE_MASK(pipe)        ((uint16_t)(1U << (pipe)))

/* USBFS peripheral selection in RA PFS.PSEL[28:24]. */
#define USB_PFS_PSEL_USB_FS        0x13U

/* -----------------------------------------------------------------------
 * CDC descriptors (Device mode)
 * ----------------------------------------------------------------------- */
static const uint8_t g_usb_dev_desc[] = {
    18U, USB_DESC_DEVICE,
    0x00U, 0x02U,     /* USB 2.00 */
    0x02U,            /* Communications Device Class */
    0x00U,
    0x00U,
    64U,              /* EP0 max packet */
    0x34U, 0x12U,     /* VID 0x1234 */
    0x78U, 0x56U,     /* PID 0x5678 */
    0x00U, 0x01U,     /* bcdDevice */
    1U, 2U, 3U,
    1U
};

static const uint8_t g_usb_cfg_desc[] = {
    /* Configuration descriptor */
    9U, USB_DESC_CONFIGURATION,
    0x43U, 0x00U,     /* total length = 67 */
    2U,               /* two interfaces */
    USB_CDC_CONFIG_VALUE,
    0U,
    0x80U,
    50U,

    /* Communication interface */
    9U, 0x04U,
    USB_CDC_COMM_INTERFACE, 0U,
    1U,
    0x02U, 0x02U, 0x01U,
    0U,

    /* CDC Header functional descriptor */
    5U, 0x24U, 0x00U, 0x10U, 0x01U,
    /* CDC Call Management functional descriptor */
    5U, 0x24U, 0x01U, 0x00U, USB_CDC_DATA_INTERFACE,
    /* CDC ACM functional descriptor */
    4U, 0x24U, 0x02U, 0x02U,
    /* CDC Union functional descriptor */
    5U, 0x24U, 0x06U, USB_CDC_COMM_INTERFACE, USB_CDC_DATA_INTERFACE,

    /* EP3 IN interrupt notification */
    7U, 0x05U, USB_CDC_INT_IN_EP_ADDR, 0x03U,
    8U, 0x00U,
    0x10U,

    /* Data interface */
    9U, 0x04U,
    USB_CDC_DATA_INTERFACE, 0U,
    2U,
    0x0AU, 0x00U, 0x00U,
    0U,

    /* EP2 OUT bulk */
    7U, 0x05U, USB_CDC_BULK_OUT_EP_ADDR, 0x02U,
    64U, 0x00U,
    0U,

    /* EP1 IN bulk */
    7U, 0x05U, USB_CDC_BULK_IN_EP_ADDR, 0x02U,
    64U, 0x00U,
    0U
};

static const uint8_t g_usb_string_langid[] = {
    4U, USB_DESC_STRING, 0x09U, 0x04U
};

static const uint8_t g_usb_string_manufacturer[] = {
    16U, USB_DESC_STRING,
    'R', 0U, 'e', 0U, 'n', 0U, 'e', 0U, 's', 0U, 'a', 0U, 's', 0U
};

static const uint8_t g_usb_string_product[] = {
    38U, USB_DESC_STRING,
    'T', 0U, 'i', 0U, 'n', 0U, 'y', 0U, 'M', 0U, 'L', 0U, ' ', 0U,
    'U', 0U, 'S', 0U, 'B', 0U, ' ', 0U, 'C', 0U, 'D', 0U, 'C', 0U,
    ' ', 0U, 'A', 0U, 'C', 0U, 'M', 0U
};

static const uint8_t g_usb_string_serial[] = {
    28U, USB_DESC_STRING,
    'R', 0U, 'A', 0U, '6', 0U, 'M', 0U, '5', 0U, '-', 0U, 'C', 0U,
    'D', 0U, 'C', 0U, '-', 0U, '0', 0U, '0', 0U, '1', 0U
};

/* -----------------------------------------------------------------------
 * Utility helpers
 * ----------------------------------------------------------------------- */
static drv_status_t usb_wait_reg16(volatile uint16_t *reg,
                                   uint16_t mask,
                                   uint16_t expected,
                                   uint32_t timeout)
{
    while (((*reg) & mask) != expected)
    {
        if (timeout == 0U)
        {
            return DRV_TIMEOUT;
        }
        timeout--;
    }
    return DRV_OK;
}

static drv_status_t usb_enable_module_clock(void)
{
    USB_SYSCFG |= USB_SYSCFG_SCKE;
    return usb_wait_reg16(&USB_SYSCFG,
                          USB_SYSCFG_SCKE,
                          USB_SYSCFG_SCKE,
                          DRV_TIMEOUT_TICKS);
}

static void usb_release_transceiver_fix(void)
{
    USB_DPUSR0R_FS &= (uint32_t)~USB_DPUSR0R_FS_FIXPHY0;
}

static void usb_device_reset_runtime_state(void)
{
    g_usb_dev_address = 0U;
    g_usb_dev_configured = 0U;
    g_usb_dev_host_ready = 0U;
    g_dev_pending_request = USB_DEV_PENDING_NONE;
    g_usb_interface_alt[USB_CDC_COMM_INTERFACE] = 0U;
    g_usb_interface_alt[USB_CDC_DATA_INTERFACE] = 0U;
    g_usb_control_line_state = 0U;
    g_usb_send_break_duration = 0U;
    g_usb_ctrl_in_data = NULL;
    g_usb_ctrl_in_remaining = 0U;
}

static uint8_t usb_device_vbus_present_stable(void)
{
    uint16_t sample0;
    uint16_t sample1;
    uint16_t sample2;

    do
    {
        sample0 = USB_INTSTS0;
        for (volatile uint32_t delay = 0U; delay < 200U; delay++)
        {
            __asm volatile ("nop");
        }

        sample1 = USB_INTSTS0;
        for (volatile uint32_t delay = 0U; delay < 200U; delay++)
        {
            __asm volatile ("nop");
        }

        sample2 = USB_INTSTS0;
    } while ((((sample0 ^ sample1) & USB_INT_VBSTS) != 0U) || (((sample1 ^ sample2) & USB_INT_VBSTS) != 0U));

    return (uint8_t)(((sample0 & USB_INT_VBSTS) != 0U) ? 1U : 0U);
}

static void usb_device_force_detach(uint16_t intsts0)
{
    g_usb_device_attach_debounce = 0U;
    g_usb_vbus_absent_count = 0U;
    g_usb_enum_suspend_count = 0U;
    usb_device_abort_bulk_in(USB_PIPE_CDC_DEV_BULK_IN);
    usb_device_reset_runtime_state();
    USB_SYSCFG = (uint16_t)(USB_SYSCFG & (uint16_t)~USB_SYSCFG_DPRPU);
    g_usb_device_pullup_enabled = 0U;
    usb_debug_trace_push(USB_DBG_EVT_STATE,
                         USB_DBG_STATE_PULLUP_DISABLED,
                         USB_SYSCFG,
                         intsts0,
                         USB_SYSSTS0);
}

static void usb_device_update_pullup(uint16_t intsts0)
{
    uint8_t vbus_present = usb_device_vbus_present_stable();

    if (g_usb_device_pullup_enabled != 0U)
    {
        if (vbus_present != 0U)
        {
            /* VBUS is stable — reset absent counter. */
            g_usb_vbus_absent_count = 0U;

            /* Stuck-enumeration recovery: if the device has been in SUSPEND
             * state (DVSQ bit 6 set) for more than ~200 ms without being
             * configured, the host has likely given up.  Force a clean
             * disconnect/reconnect so the host re-enumerates from scratch. */
            if (g_usb_dev_configured == 0U)
            {
                if ((intsts0 & 0x0040U) != 0U) /* DVSQ bit 6 = SUSPEND */
                {
                    if (g_usb_enum_suspend_count < 200U)
                    {
                        g_usb_enum_suspend_count++;
                    }
                    else
                    {
                        g_usb_enum_suspend_count = 0U;
                        usb_device_force_detach(intsts0);
                        return;
                    }
                }
                else
                {
                    g_usb_enum_suspend_count = 0U;
                }
            }
            else
            {
                g_usb_enum_suspend_count = 0U;
            }

            return;
        }

        /* VBUS absent: require 50 consecutive absent polls (~50 ms) before
         * forcing detach.  This tolerates brief glitches during enumeration
         * while still recovering from a genuine unplug event. */
        if (g_usb_vbus_absent_count < 50U)
        {
            g_usb_vbus_absent_count++;
            return;
        }

        usb_device_force_detach(intsts0);
        return;
    }

    if (vbus_present == 0U)
    {
        g_usb_device_attach_debounce = 0U;
        return;
    }

    if (g_usb_device_attach_debounce < 10U)
    {
        g_usb_device_attach_debounce++;
        return;
    }

    /* Re-configure bulk IN/OUT pipes before asserting pull-up so
     * the endpoint descriptors are consistent with hardware state
     * on the first SET_CONFIGURATION after re-enumeration. */
    USB_PIPESEL = USB_PIPE_CDC_DEV_BULK_IN;
    USB_PIPECFG = (uint16_t)(USB_PIPECFG_BULK | USB_PIPECFG_DIR | USB_CDC_BULK_IN_EP_NUM);
    USB_PIPEMAXP = 64U;

    USB_PIPESEL = USB_PIPE_CDC_DEV_BULK_OUT;
    USB_PIPECFG = (uint16_t)(USB_PIPECFG_BULK | USB_CDC_BULK_OUT_EP_NUM);
    USB_PIPEMAXP = 64U;

    USB_PIPESEL = 0U;

    USB_SYSCFG = (uint16_t)(USB_SYSCFG | USB_SYSCFG_DPRPU);
    g_usb_device_pullup_enabled = 1U;
    usb_debug_trace_push(USB_DBG_EVT_STATE,
                         USB_DBG_STATE_PULLUP_ENABLED,
                         USB_SYSCFG,
                         g_usb_device_attach_debounce,
                         intsts0);
}

static void usb_init_fifo_access_width(void)
{
    USB_CFIFOSEL = USB_FIFOSEL_MBW_16;
    USB_D0FIFOSEL = USB_FIFOSEL_MBW_16;
    USB_D1FIFOSEL = USB_FIFOSEL_MBW_16;
}

static void usb_device_module_register_clear(void)
{
    USB_DVSTCTR0 = 0U;
    USB_DCPCTR = USB_CTR_SQSET;
    USB_PIPECTR(1U) = 0U;
    USB_PIPECTR(2U) = 0U;
    USB_PIPECTR(3U) = 0U;
    USB_PIPECTR(4U) = 0U;
    USB_PIPECTR(5U) = 0U;
    USB_PIPECTR(6U) = 0U;
    USB_PIPECTR(7U) = 0U;
    USB_PIPECTR(8U) = 0U;
    USB_PIPECTR(9U) = 0U;
    USB_BRDYENB = 0U;
    USB_NRDYENB = 0U;
    USB_BEMPENB = 0U;
    USB_BRDYSTS = 0U;
    USB_NRDYSTS = 0U;
    USB_BEMPSTS = 0U;
}

static void usb_device_bus_reset(void)
{
    usb_device_reset_runtime_state();

    /* Keep the peripheral reset path as close as practical to FSP: only
     * restore the default control-pipe software-visible state here. */
    USB_DCPCFG = 0U;
    USB_DCPMAXP = 64U;
    USB_BEMPENB = (uint16_t)(USB_BEMPENB & (uint16_t)~USB_PIPE_MASK(0U));
    usb_clear_bemp(0U);

    usb_debug_trace_push(USB_DBG_EVT_STATE,
                         USB_DBG_STATE_BUS_RESET_REARM,
                         USB_DCPCTR,
                         USB_DCPCFG,
                         USB_DCPMAXP);
}

static void usb_delay_cycles(uint32_t cycles)
{
    while (cycles > 0U)
    {
        __asm volatile ("nop");
        cycles--;
    }
}

static void usb_mstp_release(void)
{
    RWP_Unlock_Clock_MSTP();
    MSTPCRB &= (uint32_t)~(1UL << USBFS_MSTP_BIT);
    RWP_Lock_Clock_MSTP();
}

static void usb_pfs_unlock(void)
{
    PWPR = 0x00U;
    PWPR = 0x40U;
}

static void usb_pfs_lock(void)
{
    PWPR = 0x80U;
}

static void usb_configure_common_pins(void)
{
    usb_pfs_unlock();

    /* P407 is the board USBFS_VBUS sense input selected in configuration.xml. */
    PmnPFS(4, 7) = (uint32_t)(PmnPFS_PMR | PmnPFS_PSEL(USB_PFS_PSEL_USB_FS));

    usb_pfs_lock();
}

static void usb_configure_host_pins(void)
{
    usb_pfs_unlock();

    /* P501 is the board USBFS_OVRCURA input selected in configuration.xml. */
    PmnPFS(5, 1) = (uint32_t)(PmnPFS_PMR | PmnPFS_PSEL(USB_PFS_PSEL_USB_FS));

    usb_pfs_lock();
}

static void usb_fifo_select_pipe(uint8_t pipe, uint8_t is_dcp_in)
{
    uint16_t sel = USB_CFIFOSEL;
    uint16_t expect;
    uint32_t timeout = DRV_TIMEOUT_TICKS;

    sel &= (uint16_t)~(USB_CFIFOSEL_ISEL | USB_CFIFOSEL_CURPIPE_MASK);
    sel |= (uint16_t)(pipe & (uint8_t)USB_CFIFOSEL_CURPIPE_MASK);
    if (is_dcp_in != 0U)
    {
        sel |= USB_CFIFOSEL_ISEL;
    }
    USB_CFIFOSEL = sel;

    expect = (uint16_t)(sel & (USB_CFIFOSEL_ISEL | USB_CFIFOSEL_CURPIPE_MASK));
    while (((USB_CFIFOSEL & (USB_CFIFOSEL_ISEL | USB_CFIFOSEL_CURPIPE_MASK)) != expect) && (timeout > 0U))
    {
        timeout--;
    }
}

static void usb_clear_brdy(uint8_t pipe)
{
    USB_BRDYSTS = (uint16_t)~(1U << pipe);
}

static void usb_clear_bemp(uint8_t pipe)
{
    USB_BEMPSTS = (uint16_t)~(1U << pipe);
}

static void usb_clear_nrdy(uint8_t pipe)
{
    USB_NRDYSTS = (uint16_t)~(1U << pipe);
}

static void usb_clear_intsts0(uint16_t mask)
{
    USB_INTSTS0 = (uint16_t)~mask;
}

static drv_status_t usb_fifo_wait_ready(void)
{
    return usb_wait_reg16(&USB_CFIFOCTR, USB_FIFOCTR_FRDY, USB_FIFOCTR_FRDY, DRV_TIMEOUT_TICKS);
}

static void usb_cfifo_set_access_width(uint16_t width)
{
    USB_CFIFOSEL = (uint16_t)((USB_CFIFOSEL & (uint16_t)~USB_FIFOSEL_MBW_MASK) | width);
}

static drv_status_t usb_fifo_write(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    if (usb_fifo_wait_ready() != DRV_OK)
    {
        return DRV_TIMEOUT;
    }

    for (i = 0U; (uint16_t)(i + 1U) < len; i += 2U)
    {
        uint16_t v = (uint16_t)data[i] | (uint16_t)((uint16_t)data[i + 1U] << 8);
        USB_CFIFO16 = v;
    }

    if ((len & 0x0001U) != 0U)
    {
        usb_cfifo_set_access_width(USB_FIFOSEL_MBW_8);
        USB_CFIFO8 = data[len - 1U];
        usb_cfifo_set_access_width(USB_FIFOSEL_MBW_16);
    }

    return DRV_OK;
}

static uint16_t usb_fifo_read(uint8_t *data, uint16_t cap)
{
    uint16_t dtln = (uint16_t)(USB_CFIFOCTR & USB_FIFOCTR_DTLN_MASK);
    uint16_t i;
    uint16_t use_len = (dtln < cap) ? dtln : cap;

    if (usb_fifo_wait_ready() != DRV_OK)
    {
        return 0U;
    }

    for (i = 0U; (uint16_t)(i + 1U) < use_len; i += 2U)
    {
        uint16_t v = USB_CFIFO16;
        data[i] = (uint8_t)(v & 0x00FFU);
        data[i + 1U] = (uint8_t)((v >> 8) & 0x00FFU);
    }

    if ((use_len & 0x0001U) != 0U)
    {
        usb_cfifo_set_access_width(USB_FIFOSEL_MBW_8);
        data[use_len - 1U] = USB_CFIFO8;
        usb_cfifo_set_access_width(USB_FIFOSEL_MBW_16);
    }

    if (dtln > cap)
    {
        /* Drop remaining bytes in FIFO packet to recover FIFO state. */
        uint16_t remain = (uint16_t)(dtln - cap);

        while (remain > 1U)
        {
            (void)USB_CFIFO16;
            remain = (uint16_t)(remain - 2U);
        }

        if (remain != 0U)
        {
            usb_cfifo_set_access_width(USB_FIFOSEL_MBW_8);
            (void)USB_CFIFO8;
            usb_cfifo_set_access_width(USB_FIFOSEL_MBW_16);
        }
    }

    return use_len;
}

static void usb_pipe_set_pid(uint8_t pipe, uint16_t pid)
{
    if (pipe == 0U)
    {
        USB_DCPCTR = (uint16_t)((USB_DCPCTR & (uint16_t)~USB_CTR_PID_MASK) | (pid & USB_CTR_PID_MASK));
    }
    else
    {
        volatile uint16_t *ctr = &USB_PIPECTR(pipe);
        *ctr = (uint16_t)((*ctr & (uint16_t)~USB_CTR_PID_MASK) | (pid & USB_CTR_PID_MASK));
    }
}

static void usb_pipe_clear_buffer(uint8_t pipe)
{
    volatile uint16_t *ctr;

    if (pipe == 0U)
    {
        ctr = &USB_DCPCTR;
    }
    else
    {
        ctr = &USB_PIPECTR(pipe);
    }

    *ctr = (uint16_t)(*ctr | USB_CTR_ACLRM);
    *ctr = (uint16_t)(*ctr & (uint16_t)~USB_CTR_ACLRM);
}

static void usb_device_abort_bulk_in(uint8_t pipe)
{
    usb_pipe_set_pid(pipe, USB_CTR_PID_NAK);
    usb_pipe_clear_buffer(pipe);
    usb_clear_bemp(pipe);
    usb_clear_nrdy(pipe);
}

static drv_status_t usb_device_wait_bulk_in_complete(uint8_t pipe)
{
    uint16_t mask = USB_PIPE_MASK(pipe);
    uint32_t timeout = USB_CDC_DEV_TX_TIMEOUT_TICKS;

    while ((USB_BEMPSTS & mask) == 0U)
    {
        if ((g_usb_dev_configured == 0U) || (USB_Dev_IsHostReady() == 0U))
        {
            return DRV_ERR;
        }

        if ((USB_NRDYSTS & mask) != 0U)
        {
            usb_clear_nrdy(pipe);
            return DRV_TIMEOUT;
        }

        if ((timeout & (USB_CDC_DEV_TX_POLL_STRIDE - 1UL)) == 0UL)
        {
            USB_PollEvents();
        }

        if (timeout == 0U)
        {
            return DRV_TIMEOUT;
        }

        timeout--;
    }

    return DRV_OK;
}

static void usb_device_control_in_reset(void)
{
    g_usb_ctrl_in_data = NULL;
    g_usb_ctrl_in_remaining = 0U;
    USB_BEMPENB = (uint16_t)(USB_BEMPENB & (uint16_t)~USB_PIPE_MASK(0U));
    usb_clear_bemp(0U);
}

static drv_status_t usb_device_prime_control_in_packet(void)
{
    uint16_t max_packet = (uint16_t)(USB_DCPMAXP & 0x007FU);
    uint16_t packet_len;
    uint16_t remaining_before;

    if (max_packet == 0U)
    {
        max_packet = 64U;
    }

    remaining_before = g_usb_ctrl_in_remaining;
    packet_len = g_usb_ctrl_in_remaining;
    if (packet_len > max_packet)
    {
        packet_len = max_packet;
    }

    usb_pipe_set_pid(0U, USB_CTR_PID_NAK);
    usb_fifo_select_pipe(0U, 1U);
    usb_clear_bemp(0U);

    if ((packet_len != 0U) && (usb_fifo_write(g_usb_ctrl_in_data, packet_len) != DRV_OK))
    {
        return DRV_TIMEOUT;
    }

    if ((packet_len < max_packet) || ((packet_len == 0U) && (g_usb_ctrl_in_remaining == 0U)))
    {
        USB_CFIFOCTR |= USB_FIFOCTR_BVAL;
    }

    if (packet_len != 0U)
    {
        g_usb_ctrl_in_data += packet_len;
    }
    g_usb_ctrl_in_remaining = (uint16_t)(g_usb_ctrl_in_remaining - packet_len);

    if (g_usb_ctrl_in_remaining != 0U)
    {
        USB_BEMPENB = (uint16_t)(USB_BEMPENB | USB_PIPE_MASK(0U));
    }
    else
    {
        USB_BEMPENB = (uint16_t)(USB_BEMPENB & (uint16_t)~USB_PIPE_MASK(0U));
    }

    usb_debug_trace_push(USB_DBG_EVT_STATE,
                         USB_DBG_STATE_CTRL_IN_PACKET,
                         packet_len,
                         g_usb_ctrl_in_remaining,
                         remaining_before);

    usb_pipe_set_pid(0U, USB_CTR_PID_BUF);
    return DRV_OK;
}

static drv_status_t usb_device_send_control_data(const uint8_t *data, uint16_t len)
{
    g_usb_ctrl_in_data = data;
    g_usb_ctrl_in_remaining = len;

    usb_fifo_select_pipe(0U, 1U);
    USB_CFIFOCTR = USB_FIFOCTR_BCLR;

    if (usb_device_prime_control_in_packet() != DRV_OK)
    {
        return DRV_TIMEOUT;
    }

    return DRV_OK;
}

static void usb_stall_control(uint16_t reason)
{
    usb_device_control_in_reset();
    usb_debug_trace_push(USB_DBG_EVT_STALL,
                         reason,
                         g_usb_debug_last_setup_request,
                         USB_DCPCTR,
                         USB_INTSTS0);
    usb_pipe_set_pid(0U, USB_CTR_PID_STALL);
}

static void usb_device_finish_status_stage(uint16_t ctsq)
{
    if ((USB_DCPCTR & USB_CTR_PID_MASK) == USB_CTR_PID_STALL)
    {
        return;
    }

    usb_device_control_in_reset();

    usb_debug_trace_push(USB_DBG_EVT_STATE,
                         USB_DBG_STATE_STATUS_STAGE,
                         ctsq,
                         USB_DCPCTR,
                         0U);
    usb_pipe_set_pid(0U, USB_CTR_PID_BUF);
    USB_DCPCTR |= USB_CTR_CCPL;
    if (g_usb_dev_address != 0)
    {
        USB_USBADDR = (uint16_t)(g_usb_dev_address & 0x007FU);
    }
}

static const uint8_t *usb_device_get_descriptor(uint8_t dtype, uint8_t dindex, uint16_t *len)
{
    switch (dtype)
    {
        case USB_DESC_DEVICE:
            *len = (uint16_t)sizeof(g_usb_dev_desc);
            return g_usb_dev_desc;

        case USB_DESC_CONFIGURATION:
            *len = (uint16_t)sizeof(g_usb_cfg_desc);
            return g_usb_cfg_desc;

        case USB_DESC_STRING:
            switch (dindex)
            {
                case 0U:
                    *len = (uint16_t)sizeof(g_usb_string_langid);
                    return g_usb_string_langid;

                case 1U:
                    *len = (uint16_t)sizeof(g_usb_string_manufacturer);
                    return g_usb_string_manufacturer;

                case 2U:
                    *len = (uint16_t)sizeof(g_usb_string_product);
                    return g_usb_string_product;

                case 3U:
                    *len = (uint16_t)sizeof(g_usb_string_serial);
                    return g_usb_string_serial;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    *len = 0U;
    return NULL;
}

static drv_status_t usb_device_complete_set_line_coding(void)
{
    uint8_t buf[7];

    if ((USB_BRDYSTS & 0x0001U) == 0U)
    {
        return DRV_ERR;
    }

    usb_fifo_select_pipe(0U, USB_DIR_OUT);
    usb_clear_brdy(0U);
    if (usb_fifo_read(buf, (uint16_t)sizeof(buf)) != (uint16_t)sizeof(buf))
    {
        return DRV_ERR;
    }

    g_line_coding.baudrate = (uint32_t)buf[0]
                           | ((uint32_t)buf[1] << 8)
                           | ((uint32_t)buf[2] << 16)
                           | ((uint32_t)buf[3] << 24);
    g_line_coding.stop_bits = buf[4];
    g_line_coding.parity = buf[5];
    g_line_coding.data_bits = buf[6];
    g_dev_pending_request = USB_DEV_PENDING_NONE;

    usb_debug_trace_push(USB_DBG_EVT_LINE_CODING,
                         (uint16_t)(g_line_coding.baudrate & 0xFFFFU),
                         (uint16_t)((g_line_coding.baudrate >> 16) & 0xFFFFU),
                         (uint16_t)(((uint16_t)g_line_coding.stop_bits << 8) | g_line_coding.parity),
                         g_line_coding.data_bits);

    /* Release the status stage only after the OUT data payload was consumed. */
    usb_pipe_set_pid(0U, USB_CTR_PID_BUF);
    return DRV_OK;
}

/* -----------------------------------------------------------------------
 * Device CDC setup packet handling
 * ----------------------------------------------------------------------- */
static void usb_device_handle_standard_request(const usb_setup_packet_t *s)
{
    switch (s->bRequest)
    {
        case USB_REQ_GET_STATUS:
        {
            uint8_t status[2] = {0U, 0U};
            uint8_t recipient = (uint8_t)(s->bmRequestType & 0x1FU);
            uint8_t endpoint = (uint8_t)(s->wIndex & 0x00FFU);
            uint16_t len = (s->wLength < 2U) ? s->wLength : 2U;

            if (((s->bmRequestType & USB_REQ_DIR_IN) == 0U) || (len == 0U))
            {
                break;
            }

            if (recipient == USB_REQ_RECIP_DEVICE)
            {
                /* Bus-powered, remote wakeup disabled. */
            }
            else if (recipient == USB_REQ_RECIP_INTERFACE)
            {
                if (s->wIndex >= (uint16_t)sizeof(g_usb_interface_alt))
                {
                    break;
                }
            }
            else if (recipient == 0x02U)
            {
                if ((endpoint != 0x00U)
                    && (endpoint != USB_CDC_BULK_IN_EP_ADDR)
                    && (endpoint != USB_CDC_BULK_OUT_EP_ADDR)
                    && (endpoint != USB_CDC_INT_IN_EP_ADDR))
                {
                    break;
                }
            }
            else
            {
                break;
            }

            (void)usb_device_send_control_data(status, len);
            return;
        }

        case USB_REQ_GET_DESCRIPTOR:
        {
            const uint8_t *desc;
            uint16_t desc_len;
            uint16_t reply_len;

            if (s->bmRequestType != (uint8_t)(USB_REQ_DIR_IN | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE))
            {
                break;
            }

            desc = usb_device_get_descriptor((uint8_t)((s->wValue >> 8) & 0x00FFU),
                                             (uint8_t)(s->wValue & 0x00FFU),
                                             &desc_len);
            if (desc == NULL)
            {
                break;
            }

            reply_len = (s->wLength < desc_len) ? s->wLength : desc_len;
            usb_debug_trace_push(USB_DBG_EVT_STATE,
                                 USB_DBG_STATE_GET_DESCRIPTOR,
                                 s->wValue,
                                 reply_len,
                                 0U);
            (void)usb_device_send_control_data(desc, reply_len);
            return;
        }

        case USB_REQ_SET_ADDRESS:
            if ((s->bmRequestType == (uint8_t)(USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE))
                && (s->wIndex == 0U)
                && (s->wLength == 0U)
                && ((s->wValue & 0xFF80U) == 0U))
            {
                g_usb_dev_address = (uint8_t)(s->wValue & 0x7FU);
                usb_debug_trace_push(USB_DBG_EVT_STATE,
                                     USB_DBG_STATE_SET_ADDRESS,
                                     g_usb_dev_address,
                                     0U,
                                     0U);
                usb_pipe_set_pid(0U, USB_CTR_PID_BUF);
                return;
            }
            break;

        case USB_REQ_GET_CONFIGURATION:
            if ((s->bmRequestType == (uint8_t)(USB_REQ_DIR_IN | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE))
                && (s->wIndex == 0U)
                && (s->wLength >= 1U))
            {
                uint8_t config = (g_usb_dev_configured != 0U) ? USB_CDC_CONFIG_VALUE : 0U;
                (void)usb_device_send_control_data(&config, 1U);
                return;
            }
            break;

        case USB_REQ_SET_CONFIGURATION:
            if ((s->bmRequestType == (uint8_t)(USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE))
                && (s->wIndex == 0U)
                && (s->wLength == 0U)
                && (((s->wValue & 0x00FFU) == 0U) || ((s->wValue & 0x00FFU) == USB_CDC_CONFIG_VALUE)))
            {
                g_usb_dev_configured = (uint8_t)(s->wValue & 0x00FFU);
                if (g_usb_dev_configured == 0U)
                {
                    g_usb_dev_host_ready = 0U;
                }
                g_usb_interface_alt[USB_CDC_COMM_INTERFACE] = 0U;
                g_usb_interface_alt[USB_CDC_DATA_INTERFACE] = 0U;
                usb_debug_trace_push(USB_DBG_EVT_STATE,
                                     USB_DBG_STATE_SET_CONFIGURATION,
                                     g_usb_dev_configured,
                                     0U,
                                     0U);
                usb_pipe_set_pid(0U, USB_CTR_PID_BUF);
                return;
            }
            break;

        case USB_REQ_GET_INTERFACE:
            if ((s->bmRequestType == (uint8_t)(USB_REQ_DIR_IN | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_INTERFACE))
                && (s->wIndex < (uint16_t)sizeof(g_usb_interface_alt))
                && (s->wLength >= 1U))
            {
                uint8_t alt = g_usb_interface_alt[s->wIndex];
                (void)usb_device_send_control_data(&alt, 1U);
                return;
            }
            break;

        case USB_REQ_SET_INTERFACE:
            if ((s->bmRequestType == (uint8_t)(USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_INTERFACE))
                && (s->wIndex < (uint16_t)sizeof(g_usb_interface_alt))
                && (s->wValue == 0U)
                && (s->wLength == 0U))
            {
                g_usb_interface_alt[s->wIndex] = 0U;
                usb_debug_trace_push(USB_DBG_EVT_STATE,
                                     USB_DBG_STATE_SET_INTERFACE,
                                     s->wIndex,
                                     0U,
                                     0U);
                usb_pipe_set_pid(0U, USB_CTR_PID_BUF);
                return;
            }
            break;

        default:
            break;
    }

    usb_stall_control(USB_DBG_STALL_UNSUPPORTED_STANDARD);
}

static void usb_device_handle_class_request(const usb_setup_packet_t *s)
{
    if ((s->bmRequestType == (uint8_t)(USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE))
        && (s->bRequest == USB_CDC_REQ_SET_LINE_CODING)
        && (s->wIndex == USB_CDC_COMM_INTERFACE)
        && (s->wLength == 7U))
    {
        g_dev_pending_request = USB_DEV_PENDING_SET_LINE_CODING;
        usb_debug_trace_push(USB_DBG_EVT_STATE,
                             USB_DBG_STATE_SET_LINE_CODING_PENDING,
                             s->wLength,
                             0U,
                             0U);
        usb_pipe_set_pid(0U, USB_CTR_PID_BUF);
        return;
    }

    if ((s->bmRequestType == (uint8_t)(USB_REQ_DIR_IN | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE))
        && (s->bRequest == USB_CDC_REQ_GET_LINE_CODING)
        && (s->wIndex == USB_CDC_COMM_INTERFACE)
        && (s->wLength >= 7U))
    {
        uint8_t buf[7];
        buf[0] = (uint8_t)(g_line_coding.baudrate & 0xFFU);
        buf[1] = (uint8_t)((g_line_coding.baudrate >> 8) & 0xFFU);
        buf[2] = (uint8_t)((g_line_coding.baudrate >> 16) & 0xFFU);
        buf[3] = (uint8_t)((g_line_coding.baudrate >> 24) & 0xFFU);
        buf[4] = g_line_coding.stop_bits;
        buf[5] = g_line_coding.parity;
        buf[6] = g_line_coding.data_bits;
        usb_debug_trace_push(USB_DBG_EVT_STATE,
                             USB_DBG_STATE_GET_LINE_CODING,
                             7U,
                             0U,
                             0U);
        (void)usb_device_send_control_data(buf, 7U);
        return;
    }

    if ((s->bmRequestType == (uint8_t)(USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE))
        && (s->bRequest == USB_CDC_REQ_SET_CONTROL_LINE_STATE)
        && (s->wIndex == USB_CDC_COMM_INTERFACE)
        && (s->wLength == 0U))
    {
        if ((g_usb_control_line_state != 0U) && (s->wValue == 0U))
        {
            g_usb_dev_host_ready = 0U;
            usb_device_abort_bulk_in(USB_PIPE_CDC_DEV_BULK_IN);
        }
        else if (s->wValue != 0U)
        {
            g_usb_dev_host_ready = 1U;
        }
        g_usb_control_line_state = s->wValue;
        usb_debug_trace_push(USB_DBG_EVT_STATE,
                             USB_DBG_STATE_SET_CONTROL_LINE_STATE,
                             s->wValue,
                             0U,
                             0U);
        usb_pipe_set_pid(0U, USB_CTR_PID_BUF);
        return;
    }

    if ((s->bmRequestType == (uint8_t)(USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE))
        && (s->bRequest == USB_CDC_REQ_SEND_BREAK)
        && (s->wIndex == USB_CDC_COMM_INTERFACE)
        && (s->wLength == 0U))
    {
        g_usb_send_break_duration = s->wValue;
        usb_debug_trace_push(USB_DBG_EVT_STATE,
                             USB_DBG_STATE_SEND_BREAK,
                             s->wValue,
                             0U,
                             0U);
        usb_pipe_set_pid(0U, USB_CTR_PID_BUF);
        return;
    }

    usb_stall_control(USB_DBG_STALL_UNSUPPORTED_CLASS);
}

static void usb_device_handle_setup(void)
{
    usb_setup_packet_t s;

    usb_clear_intsts0(USB_INT_VALID);

    {
    uint16_t req = USB_USBREQ;

    s.bmRequestType = (uint8_t)(req & 0x00FFU);
    s.bRequest = (uint8_t)((req >> 8) & 0x00FFU);
    s.wValue = USB_USBVAL;
    s.wIndex = USB_USBINDX;
    s.wLength = USB_USBLENG;
    }

    g_usb_debug_last_setup_request = usb_debug_pack_request(&s);
    usb_debug_trace_push(USB_DBG_EVT_SETUP,
                         g_usb_debug_last_setup_request,
                         s.wValue,
                         s.wIndex,
                         s.wLength);

    if ((s.bmRequestType & 0x60U) == USB_REQ_TYPE_STANDARD)
    {
        usb_device_handle_standard_request(&s);
        return;
    }

    if ((s.bmRequestType & 0x60U) == USB_REQ_TYPE_CLASS)
    {
        usb_device_handle_class_request(&s);
        return;
    }

    usb_stall_control(USB_DBG_STALL_UNSUPPORTED_TYPE);
}

static void usb_device_handle_control_stage(uint16_t intsts0)
{
    uint16_t ctsq = (uint16_t)(intsts0 & USB_INT_CTSQ_MASK);

    switch (ctsq)
    {
        case USB_CS_RDDS:
        case USB_CS_WRDS:
        case USB_CS_WRND:
            usb_device_handle_setup();
            if (ctsq == USB_CS_WRND)
            {
                usb_device_finish_status_stage(ctsq);
            }
            break;

        case USB_CS_RDSS:
        case USB_CS_WRSS:
            usb_device_finish_status_stage(ctsq);
            break;

        case USB_CS_IDST:
            break;

        case USB_CS_SQER:
        default:
            g_dev_pending_request = USB_DEV_PENDING_NONE;
            usb_stall_control(USB_DBG_STALL_CONTROL_SEQUENCE);
            break;
    }
}

/* -----------------------------------------------------------------------
 * Host-mode control transfer helpers
 * ----------------------------------------------------------------------- */
static drv_status_t usb_host_send_setup(const usb_setup_packet_t *s)
{
    USB_USBREQ = (uint16_t)(((uint16_t)s->bRequest << 8) | s->bmRequestType);
    USB_USBVAL = s->wValue;
    USB_USBINDX = s->wIndex;
    USB_USBLENG = s->wLength;
    USB_DCPCTR |= USB_CTR_SUREQ;
    return usb_wait_reg16(&USB_DCPCTR, USB_CTR_SUREQ, 0U, DRV_TIMEOUT_TICKS);
}

static drv_status_t usb_host_control_in(const usb_setup_packet_t *s, uint8_t *buf, uint16_t cap, uint16_t *actual)
{
    drv_status_t st = usb_host_send_setup(s);
    uint16_t total = 0U;

    if (st != DRV_OK)
    {
        return st;
    }

    usb_fifo_select_pipe(0U, 1U);
    usb_pipe_set_pid(0U, USB_CTR_PID_BUF);

    while (total < s->wLength)
    {
        if (usb_wait_reg16(&USB_BRDYSTS, 0x0001U, 0x0001U, DRV_TIMEOUT_TICKS) != DRV_OK)
        {
            return DRV_TIMEOUT;
        }
        USB_BRDYSTS = (uint16_t)~0x0001U;

        {
            uint16_t step = usb_fifo_read(&buf[total], (uint16_t)(cap - total));
            total = (uint16_t)(total + step);
            if (step < 64U)
            {
                break;
            }
        }
    }

    usb_pipe_set_pid(0U, USB_CTR_PID_NAK);
    if (actual != NULL)
    {
        *actual = total;
    }
    return DRV_OK;
}

static drv_status_t usb_host_control_no_data(const usb_setup_packet_t *s)
{
    return usb_host_send_setup(s);
}

static drv_status_t usb_host_reset_bus(void)
{
    USB_DVSTCTR0 |= USB_DVSTCTR0_USBRST;
    for (volatile uint32_t d = 0U; d < 10000U; d++)
    {
        __asm volatile ("nop");
    }
    USB_DVSTCTR0 &= (uint16_t)~USB_DVSTCTR0_USBRST;
    for (volatile uint32_t d = 0U; d < 10000U; d++)
    {
        __asm volatile ("nop");
    }
    return DRV_OK;
}

static uint8_t usb_host_is_attach_detected(void)
{
    uint16_t lnst = (uint16_t)(USB_SYSSTS0 & USB_SYSSTS0_LNST_MASK);
    return (uint8_t)((lnst == USB_SYSSTS0_LNST_J) || (lnst == USB_SYSSTS0_LNST_K));
}

static drv_status_t usb_host_wait_attach(void)
{
    uint32_t to = DRV_TIMEOUT_TICKS;
    while (to > 0U)
    {
        if (usb_host_is_attach_detected() != 0U)
        {
            return DRV_OK;
        }
        to--;
    }
    return DRV_TIMEOUT;
}

static void usb_host_parse_cdc_endpoints(const uint8_t *cfg, uint16_t len)
{
    uint16_t idx = 0U;
    g_host_dev.bulk_in_ep = 0U;
    g_host_dev.bulk_out_ep = 0U;
    g_host_dev.bulk_in_mps = 64U;
    g_host_dev.bulk_out_mps = 64U;

    while ((uint16_t)(idx + 1U) < len)
    {
        uint8_t dlen = cfg[idx + 0U];
        uint8_t dtype = cfg[idx + 1U];
        if ((dlen < 2U) || ((uint16_t)(idx + dlen) > len))
        {
            break;
        }

        if ((dtype == 0x05U) && (dlen >= 7U))
        {
            uint8_t ep_addr = cfg[idx + 2U];
            uint8_t attr = cfg[idx + 3U];
            uint16_t mps = (uint16_t)cfg[idx + 4U] | ((uint16_t)cfg[idx + 5U] << 8);
            if ((attr & 0x03U) == 0x02U)
            {
                if ((ep_addr & 0x80U) != 0U)
                {
                    g_host_dev.bulk_in_ep = (uint8_t)(ep_addr & 0x0FU);
                    g_host_dev.bulk_in_mps = mps;
                }
                else
                {
                    g_host_dev.bulk_out_ep = (uint8_t)(ep_addr & 0x0FU);
                    g_host_dev.bulk_out_mps = mps;
                }
            }
        }

        idx = (uint16_t)(idx + dlen);
    }
}

static void usb_host_configure_bulk_pipes(void)
{
    /* PIPE1: host BULK OUT */
    USB_PIPESEL = USB_PIPE_CDC_HOST_BULK_OUT;
    USB_PIPECFG = (uint16_t)(USB_PIPECFG_BULK | (uint16_t)(g_host_dev.bulk_out_ep & 0x0FU));
    USB_PIPEMAXP = (uint16_t)(((uint16_t)g_host_dev.address << 12) | (g_host_dev.bulk_out_mps & 0x07FFU));

    /* PIPE2: host BULK IN */
    USB_PIPESEL = USB_PIPE_CDC_HOST_BULK_IN;
    USB_PIPECFG = (uint16_t)(USB_PIPECFG_BULK | USB_PIPECFG_DIR | (uint16_t)(g_host_dev.bulk_in_ep & 0x0FU));
    USB_PIPEMAXP = (uint16_t)(((uint16_t)g_host_dev.address << 12) | (g_host_dev.bulk_in_mps & 0x07FFU));

    /* Restore PIPESEL to DCP-safe default */
    USB_PIPESEL = 0U;

    USB_BRDYENB = (uint16_t)(USB_PIPE_MASK(0U) | USB_PIPE_MASK(USB_PIPE_CDC_HOST_BULK_IN));
    USB_BEMPENB = USB_PIPE_MASK(USB_PIPE_CDC_HOST_BULK_OUT);
    USB_NRDYENB = (uint16_t)(USB_PIPE_MASK(0U)
                           | USB_PIPE_MASK(USB_PIPE_CDC_HOST_BULK_OUT)
                           | USB_PIPE_MASK(USB_PIPE_CDC_HOST_BULK_IN));
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
drv_status_t USB_Init(usb_mode_t mode)
{
    usb_debug_trace_reset();

    g_mode = mode;
    usb_device_reset_runtime_state();
    g_usb_device_pullup_enabled = 0U;
    g_usb_device_attach_debounce = 0U;
    memset(&g_host_dev, 0, sizeof(g_host_dev));
    memset(&g_host_desc, 0, sizeof(g_host_desc));

    usb_mstp_release();
    usb_configure_common_pins();

    if (usb_enable_module_clock() != DRV_OK)
    {
        usb_debug_trace_push(USB_DBG_EVT_ERROR,
                             USB_DBG_ERROR_SET_LINE_CODING_READ + 1U,
                             USB_SYSCFG,
                             0U,
                             0U);
        return DRV_TIMEOUT;
    }

    /* Match FSP bring-up: release the USBFS transceiver output fix before
     * enabling device or host signaling. */
    usb_release_transceiver_fix();

    USB_INTENB0 = 0U;
    USB_INTENB1 = 0U;
    USB_BRDYENB = 0U;
    USB_NRDYENB = 0U;
    USB_BEMPENB = 0U;

    /* Clear stale statuses before enabling USB engine. */
    USB_INTSTS0 = 0U;
    USB_INTSTS1 = 0U;
    USB_BRDYSTS = 0U;
    USB_NRDYSTS = 0U;
    USB_BEMPSTS = 0U;

    USB_BUSWAIT = USB_BUSWAIT_5;

    if (mode == USB_MODE_DEVICE_CDC)
    {
        /* Device mode: start detached and announce with DPRPU only after stable VBUS. */
        USB_SYSCFG = (uint16_t)(USB_SYSCFG_SCKE | USB_SYSCFG_USBE);
        usb_init_fifo_access_width();
        usb_device_module_register_clear();

        /* PIPE1 = BULK IN endpoint 1 for logging stream */
        USB_PIPESEL = USB_PIPE_CDC_DEV_BULK_IN;
        USB_PIPECFG = (uint16_t)(USB_PIPECFG_BULK | USB_PIPECFG_DIR | USB_CDC_BULK_IN_EP_NUM);
        USB_PIPEMAXP = 64U;

        /* PIPE2 = BULK OUT endpoint 2 for CDC data interface */
        USB_PIPESEL = USB_PIPE_CDC_DEV_BULK_OUT;
        USB_PIPECFG = (uint16_t)(USB_PIPECFG_BULK | USB_CDC_BULK_OUT_EP_NUM);
        USB_PIPEMAXP = 64U;

        /* PIPE6 = INT IN endpoint 3 for CDC notifications */
        USB_PIPESEL = USB_PIPE_CDC_DEV_INT_IN;
        USB_PIPECFG = (uint16_t)(USB_PIPECFG_INT | USB_PIPECFG_DIR | USB_CDC_INT_IN_EP_NUM);
        USB_PIPEMAXP = 8U;
        USB_PIPEPERI = 0U;

        USB_PIPESEL = 0U;

        usb_device_bus_reset();

        /* Enable BRDY interrupt for DCP (pipe 0) so the SET_LINE_CODING data
         * stage is caught reliably by interrupt rather than polling BRDYSTS
         * only at the next USB_PollEvents() tick. */
        USB_BRDYENB = USB_PIPE_MASK(0U);
        USB_INTENB0 = (uint16_t)(USB_INT_VBINT | USB_INT_DVST | USB_INT_CTRT | USB_INT_BRDY | USB_INT_NRDY | USB_INT_BEMP);
    }
    else
    {
        usb_configure_host_pins();

        /* Host mode: DCFM=1, pull-down enabled, pull-up disabled. */
        USB_SYSCFG = (uint16_t)(USB_SYSCFG_SCKE | USB_SYSCFG_USBE | USB_SYSCFG_DCFM | USB_SYSCFG_DRPD);
        usb_init_fifo_access_width();

        /* Enable board-side VBUS switch (P500 = USBFS_VBUS_EN). */
        GPIO_Config(USB_VBUS_EN_PORT, USB_VBUS_EN_PIN, GPIO_CNF_OUT_PP, GPIO_MODE_OUTPUT);
        GPIO_Write_Pin(USB_VBUS_EN_PORT, USB_VBUS_EN_PIN, GPIO_PIN_SET);

        USB_BRDYENB = USB_PIPE_MASK(0U);
        USB_NRDYENB = USB_PIPE_MASK(0U);
        USB_INTENB0 = (uint16_t)(USB_INT_VBINT | USB_INT_CTRT | USB_INT_BRDY | USB_INT_NRDY | USB_INT_BEMP);
        USB_DVSTCTR0 |= USB_DVSTCTR0_UACT;
    }

    usb_debug_trace_push(USB_DBG_EVT_INIT,
                         (uint16_t)mode,
                         USB_SYSCFG,
                         USB_INTENB0,
                         USB_BRDYENB);

    return DRV_OK;
}

void USB_Deinit(void)
{
    USB_INTENB0 = 0U;
    USB_INTENB1 = 0U;
    USB_BRDYENB = 0U;
    USB_NRDYENB = 0U;
    USB_BEMPENB = 0U;
    USB_DVSTCTR0 &= (uint16_t)~USB_DVSTCTR0_UACT;
    USB_SYSCFG = 0U;
    g_usb_device_pullup_enabled = 0U;
    g_usb_device_attach_debounce = 0U;

    if (g_mode == USB_MODE_HOST_CDC_ACM)
    {
        GPIO_Write_Pin(USB_VBUS_EN_PORT, USB_VBUS_EN_PIN, GPIO_PIN_RESET);
    }
}

void USB_Dev_SetConfigured(uint8_t configured)
{
    g_usb_dev_configured = configured;
    usb_debug_trace_push(USB_DBG_EVT_STATE,
                         USB_DBG_STATE_SW_CONFIGURED,
                         configured,
                         0U,
                         0U);
}

uint8_t USB_Dev_IsConfigured(void)
{
    return g_usb_dev_configured;
}

uint8_t USB_Dev_IsHostReady(void)
{
#if USB_DEV_CDC_REQUIRE_DTR != 0U
    return (uint8_t)(((g_usb_dev_configured != 0U) && (g_usb_dev_host_ready != 0U)) ? 1U : 0U);
#else
    return (uint8_t)((g_usb_dev_configured != 0U) ? 1U : 0U);
#endif
}

drv_status_t USB_Dev_Write(const uint8_t *data, uint32_t len)
{
    uint32_t sent = 0U;
    drv_status_t write_st;

    if ((g_mode != USB_MODE_DEVICE_CDC)
        || (g_usb_dev_configured == 0U)
        || (USB_Dev_IsHostReady() == 0U)
        || (data == NULL))
    {
        return DRV_ERR;
    }

    /* Set NAK and clear FIFO/status before starting a new transfer so
     * any stale state from a previous call does not corrupt this one. */
    usb_pipe_set_pid(USB_PIPE_CDC_DEV_BULK_IN, USB_CTR_PID_NAK);
    usb_pipe_clear_buffer(USB_PIPE_CDC_DEV_BULK_IN);
    usb_clear_bemp(USB_PIPE_CDC_DEV_BULK_IN);
    usb_clear_nrdy(USB_PIPE_CDC_DEV_BULK_IN);

    while (sent < len)
    {
        uint16_t chunk = (uint16_t)((len - sent) > 64U ? 64U : (len - sent));

        /* Step 1: NAK first — stop hardware from touching FIFO while CPU
         * is writing, per FSP usb_pstd_buf_to_fifo pattern. */
        usb_pipe_set_pid(USB_PIPE_CDC_DEV_BULK_IN, USB_CTR_PID_NAK);

        /* Step 2: Select CFIFO for PIPE1 IN direction. */
        usb_fifo_select_pipe(USB_PIPE_CDC_DEV_BULK_IN, USB_DIR_IN);

        /* Step 3: Write payload into FIFO. */
        if (usb_fifo_write(&data[sent], chunk) != DRV_OK)
        {
            usb_device_abort_bulk_in(USB_PIPE_CDC_DEV_BULK_IN);
            return DRV_TIMEOUT;
        }

        /* Step 4: For short (last) packet BVAL must be set so the USBFS
         * commits the partial FIFO content as a USB packet.  Without this
         * the hardware waits for more data and the packet is never sent.
         * (FSP: hw_usb_set_bval when data_cnt < buf_size) */
        if (chunk < 64U)
        {
            USB_CFIFOCTR |= USB_FIFOCTR_BVAL;
        }

        /* Step 5: Clear BEMP latch for PIPE1 before enabling BUF. */
        USB_BEMPSTS = (uint16_t)~(1U << USB_PIPE_CDC_DEV_BULK_IN);

        /* Step 6: Re-enable endpoint — hardware will now send the packet. */
        usb_pipe_set_pid(USB_PIPE_CDC_DEV_BULK_IN, USB_CTR_PID_BUF);

        write_st = usb_device_wait_bulk_in_complete(USB_PIPE_CDC_DEV_BULK_IN);
        if (write_st != DRV_OK)
        {
            g_usb_dev_host_ready = 0U;
            usb_device_abort_bulk_in(USB_PIPE_CDC_DEV_BULK_IN);
            return write_st;
        }

        sent += chunk;
    }

    return DRV_OK;
}

drv_status_t USB_Dev_Printf(const char *fmt, ...)
{
    char buf[192];
    int n;
    va_list ap;

    if (fmt == NULL)
    {
        return DRV_ERR;
    }

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0)
    {
        return DRV_ERR;
    }

    if ((size_t)n >= sizeof(buf))
    {
        n = (int)(sizeof(buf) - 1U);
    }

    return USB_Dev_Write((const uint8_t *)buf, (uint32_t)n);
}
drv_status_t USB_Host_Enumerate(void)
{
    uint8_t dev_desc[18];
    uint8_t cfg_hdr[9];
    uint8_t cfg_desc[256];
    uint16_t actual = 0U;
    uint16_t cfg_len;
    usb_setup_packet_t s;

    if (g_mode != USB_MODE_HOST_CDC_ACM)
    {
        return DRV_ERR;
    }

    /* 1) Attach detect */
    if (usb_host_wait_attach() != DRV_OK)
    {
        return DRV_TIMEOUT;
    }

    /* 2) Bus reset */
    if (usb_host_reset_bus() != DRV_OK)
    {
        return DRV_ERR;
    }

    /* 3) Read Device Descriptor (first 18 bytes). */
    s.bmRequestType = (uint8_t)(USB_REQ_DIR_IN | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE);
    s.bRequest = USB_REQ_GET_DESCRIPTOR;
    s.wValue = (uint16_t)((uint16_t)USB_DESC_DEVICE << 8);
    s.wIndex = 0U;
    s.wLength = 18U;
    if (usb_host_control_in(&s, dev_desc, (uint16_t)sizeof(dev_desc), &actual) != DRV_OK)
    {
        return DRV_TIMEOUT;
    }
    if (actual < 8U)
    {
        return DRV_ERR;
    }

    memcpy(g_host_desc.device_desc, dev_desc, sizeof(dev_desc));
    g_host_desc.max_packet_size_ep0 = dev_desc[7];

    USB_DCPMAXP = dev_desc[7];

    /* 4) SET_ADDRESS = 1 */
    s.bmRequestType = (uint8_t)(USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE);
    s.bRequest = USB_REQ_SET_ADDRESS;
    s.wValue = 1U;
    s.wIndex = 0U;
    s.wLength = 0U;
    if (usb_host_control_no_data(&s) != DRV_OK)
    {
        return DRV_TIMEOUT;
    }

    usb_delay_cycles(10000U);

    g_host_dev.address = 1U;
    USB_USBADDR = (uint16_t)((uint16_t)g_host_dev.address << 12);

    /* 5) Read configuration header first to obtain wTotalLength. */
    s.bmRequestType = (uint8_t)(USB_REQ_DIR_IN | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE);
    s.bRequest = USB_REQ_GET_DESCRIPTOR;
    s.wValue = (uint16_t)((uint16_t)USB_DESC_CONFIGURATION << 8);
    s.wIndex = 0U;
    s.wLength = (uint16_t)sizeof(cfg_hdr);
    if (usb_host_control_in(&s, cfg_hdr, (uint16_t)sizeof(cfg_hdr), &actual) != DRV_OK)
    {
        return DRV_TIMEOUT;
    }
    if (actual < 9U)
    {
        return DRV_ERR;
    }

    g_host_desc.config_total_length = (uint16_t)cfg_hdr[2] | ((uint16_t)cfg_hdr[3] << 8);
    g_host_desc.num_interfaces = cfg_hdr[4];
    g_host_desc.configuration_value = cfg_hdr[5];

    cfg_len = g_host_desc.config_total_length;
    if (cfg_len > (uint16_t)sizeof(cfg_desc))
    {
        cfg_len = (uint16_t)sizeof(cfg_desc);
    }

    s.wLength = cfg_len;
    if (usb_host_control_in(&s, cfg_desc, cfg_len, &actual) != DRV_OK)
    {
        return DRV_TIMEOUT;
    }
    if (actual < 9U)
    {
        return DRV_ERR;
    }

    usb_host_parse_cdc_endpoints(cfg_desc, actual);
    if ((g_host_dev.bulk_in_ep == 0U) || (g_host_dev.bulk_out_ep == 0U))
    {
        return DRV_ERR;
    }

    /* 6) SET_CONFIGURATION */
    g_host_dev.config_value = g_host_desc.configuration_value;
    s.bmRequestType = (uint8_t)(USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE);
    s.bRequest = USB_REQ_SET_CONFIGURATION;
    s.wValue = g_host_dev.config_value;
    s.wIndex = 0U;
    s.wLength = 0U;
    if (usb_host_control_no_data(&s) != DRV_OK)
    {
        return DRV_TIMEOUT;
    }

    usb_host_configure_bulk_pipes();
    g_host_dev.configured = 1U;
    return DRV_OK;
}

drv_status_t USB_Host_Send_AT_Command(char *cmd)
{
    uint32_t len;
    uint32_t sent = 0U;

    if ((g_mode != USB_MODE_HOST_CDC_ACM) || (g_host_dev.configured == 0U) || (cmd == NULL))
    {
        return DRV_ERR;
    }

    len = (uint32_t)strlen(cmd);
    usb_fifo_select_pipe(USB_PIPE_CDC_HOST_BULK_OUT, USB_DIR_OUT);

    while (sent < len)
    {
        uint16_t chunk = (uint16_t)((len - sent) > g_host_dev.bulk_out_mps
                              ? g_host_dev.bulk_out_mps : (len - sent));
        if (usb_fifo_write((const uint8_t *)&cmd[sent], chunk) != DRV_OK)
        {
            return DRV_TIMEOUT;
        }

        USB_BEMPSTS = (uint16_t)~(1U << USB_PIPE_CDC_HOST_BULK_OUT);
        usb_pipe_set_pid(USB_PIPE_CDC_HOST_BULK_OUT, USB_CTR_PID_BUF);

        if (usb_wait_reg16(&USB_BEMPSTS,
                           (uint16_t)(1U << USB_PIPE_CDC_HOST_BULK_OUT),
                           (uint16_t)(1U << USB_PIPE_CDC_HOST_BULK_OUT),
                           DRV_TIMEOUT_TICKS) != DRV_OK)
        {
            return DRV_TIMEOUT;
        }
        sent += chunk;
    }

    return DRV_OK;
}

int32_t USB_Host_Read_Response(uint8_t *buffer, uint32_t len)
{
    uint32_t recved = 0U;

    if ((g_mode != USB_MODE_HOST_CDC_ACM) || (g_host_dev.configured == 0U) || (buffer == NULL))
    {
        return -1;
    }

    usb_fifo_select_pipe(USB_PIPE_CDC_HOST_BULK_IN, USB_DIR_IN);
    usb_pipe_set_pid(USB_PIPE_CDC_HOST_BULK_IN, USB_CTR_PID_BUF);

    while (recved < len)
    {
        if (usb_wait_reg16(&USB_BRDYSTS,
                           (uint16_t)(1U << USB_PIPE_CDC_HOST_BULK_IN),
                           (uint16_t)(1U << USB_PIPE_CDC_HOST_BULK_IN),
                           DRV_TIMEOUT_TICKS) != DRV_OK)
        {
            break;
        }

        USB_BRDYSTS = (uint16_t)~(1U << USB_PIPE_CDC_HOST_BULK_IN);

        {
            uint16_t step = usb_fifo_read(&buffer[recved], (uint16_t)(len - recved));
            recved += step;
            if ((step == 0U) || (step < g_host_dev.bulk_in_mps))
            {
                break;
            }
        }
    }

    if (recved == 0U)
    {
        return -1;
    }

    return (int32_t)recved;
}

const usb_host_cdc_device_t *USB_Host_GetDeviceInfo(void)
{
    return &g_host_dev;
}

const usb_host_descriptor_snapshot_t *USB_Host_GetDescriptorSnapshot(void)
{
    return &g_host_desc;
}

uint8_t USB_Debug_PopTrace(usb_debug_trace_t *trace)
{
    uint8_t tail;

    if (trace == NULL)
    {
        return 0U;
    }

    tail = g_usb_debug_trace_tail;
    if (tail == g_usb_debug_trace_head)
    {
        return 0U;
    }

    *trace = g_usb_debug_trace[tail];
    g_usb_debug_trace_tail = (uint8_t)((tail + 1U) % USB_DEBUG_TRACE_DEPTH);
    return 1U;
}

uint16_t USB_Debug_GetDroppedTraceCount(void)
{
    return g_usb_debug_trace_dropped;
}

void USB_PollEvents(void)
{
    uint8_t pass;

    /* USB_PollEvents can be called from both usb_svc task and USB IRQ.
     * Ignore nested entry to avoid state-machine races and duplicate handling. */
    if (g_usb_poll_busy != 0U)
    {
        return;
    }
    g_usb_poll_busy = 1U;

    for (pass = 0U; pass < 8U; pass++)
    {
        uint16_t is0 = USB_INTSTS0;
        uint16_t enabled0 = USB_INTENB0;
        uint16_t active0 = (uint16_t)(is0 & enabled0);

        if (g_mode == USB_MODE_DEVICE_CDC)
        {
            usb_device_update_pullup(is0);
        }

        if ((g_mode == USB_MODE_DEVICE_CDC)
            && (g_dev_pending_request == USB_DEV_PENDING_SET_LINE_CODING)
            && ((USB_BRDYSTS & 0x0001U) != 0U))
        {
            if (usb_device_complete_set_line_coding() != DRV_OK)
            {
                usb_debug_trace_push(USB_DBG_EVT_ERROR,
                                     USB_DBG_ERROR_SET_LINE_CODING_READ,
                                     USB_BRDYSTS,
                                     USB_CFIFOCTR,
                                     USB_DCPCTR);
                usb_stall_control(USB_DBG_STALL_SET_LINE_CODING_DATA);
                g_dev_pending_request = USB_DEV_PENDING_NONE;
            }
        }

        if (active0 == 0U)
        {
            break;
        }

        /* Preserve interrupt priority, but drain all latched sources seen in
         * this invocation so EP0 state transitions are not delayed until a
         * later poll tick. */
        if ((active0 & USB_INT_RESM) != 0U)
        {
            usb_clear_intsts0(USB_INT_RESM);
            continue;
        }

        if ((active0 & USB_INT_VBINT) != 0U)
        {
            usb_clear_intsts0(USB_INT_VBINT);
            continue;
        }

        if ((active0 & USB_INT_DVST) != 0U)
        {
            uint16_t dvsq = (uint16_t)(is0 & USB_INT_DVSQ_MASK);

            usb_debug_trace_push(USB_DBG_EVT_DVST,
                                 is0,
                                 USB_SYSSTS0,
                                 USB_DVSTCTR0,
                                 (uint16_t)(((uint16_t)g_usb_dev_configured << 8) | g_usb_dev_address));

            if (g_mode == USB_MODE_DEVICE_CDC)
            {
                if (dvsq == USB_DS_DFLT)
                {
                    usb_device_bus_reset();
                }
            }

            usb_clear_intsts0(USB_INT_DVST);
            continue;
        }

        if ((active0 & USB_INT_CTRT) != 0U)
        {
            uint16_t control_status;

            usb_clear_intsts0(USB_INT_CTRT);
            control_status = USB_INTSTS0;

            usb_debug_trace_push(USB_DBG_EVT_CTRT,
                                 is0,
                                 control_status,
                                 (uint16_t)(control_status & USB_INT_CTSQ_MASK),
                                 (uint16_t)g_dev_pending_request);

            if (g_mode == USB_MODE_DEVICE_CDC)
            {
                usb_device_handle_control_stage(control_status);
            }

            continue;
        }

        if ((active0 & USB_INT_BRDY) != 0U)
        {
            /* BRDY is consumed by data path polling helpers; clear latched bit. */
            usb_clear_intsts0(USB_INT_BRDY);
            continue;
        }

        if ((active0 & USB_INT_BEMP) != 0U)
        {
            if ((g_mode == USB_MODE_DEVICE_CDC) && ((USB_BEMPSTS & USB_PIPE_MASK(0U)) != 0U))
            {
                /* Immediately NAK pipe 0 to prevent the hardware from sending
                 * a zero-length IN packet (ZLP) while the CPU refills the
                 * FIFO.  A ZLP signals premature end-of-transfer to the host,
                 * aborting control descriptor reads mid-stream. */
                usb_pipe_set_pid(0U, USB_CTR_PID_NAK);
                usb_clear_bemp(0U);

                if (g_usb_ctrl_in_remaining != 0U)
                {
                    if (usb_device_prime_control_in_packet() != DRV_OK)
                    {
                        usb_stall_control(USB_DBG_STALL_CONTROL_SEQUENCE);
                    }
                }
            }

            usb_clear_intsts0(USB_INT_BEMP);
            continue;
        }

        if ((active0 & USB_INT_NRDY) != 0U)
        {
            /* NRDY is treated as a recoverable endpoint-level error. */
            usb_clear_intsts0(USB_INT_NRDY);
            continue;
        }

        break;
    }

    g_usb_poll_busy = 0U;
}

void USBI0_IRQHandler(void)
{
    USB_PollEvents();
}

void USBI1_IRQHandler(void)
{
    USB_PollEvents();
}
