#include "test_usb.h"

#include "debug_print.h"
#include "drv_usb.h"
#include "kernel.h"
#include "rtos_config.h"

#include <stdint.h>

#define USB_TEST_TASK_PRIO        1U
#define USB_CDC_POLL_PERIOD_MS    1U
#define USB_CDC_LOG_PERIOD_MS  1000U
#define USB_HOST_BOOT_DELAY_MS  500U

static OS_TCB_t s_usb_cdc_tcb;
static OS_TCB_t s_usb_host_tcb;

static const char *usb_trace_ctsq_name(uint16_t ctsq)
{
    switch (ctsq)
    {
        case 0U: return "IDST";
        case 1U: return "RDDS";
        case 2U: return "RDSS";
        case 3U: return "WRDS";
        case 4U: return "WRSS";
        case 5U: return "WRND";
        case 6U: return "SQER";
        default: return "UNKNOWN";
    }
}

static const char *usb_trace_request_name(uint16_t packed_request)
{
    uint8_t bm_request_type = (uint8_t)(packed_request & 0x00FFU);
    uint8_t request = (uint8_t)((packed_request >> 8) & 0x00FFU);

    if ((bm_request_type & 0x60U) == 0x00U)
    {
        switch (request)
        {
            case 0x00U: return "GET_STATUS";
            case 0x05U: return "SET_ADDRESS";
            case 0x06U: return "GET_DESCRIPTOR";
            case 0x08U: return "GET_CONFIGURATION";
            case 0x09U: return "SET_CONFIGURATION";
            case 0x0AU: return "GET_INTERFACE";
            case 0x0BU: return "SET_INTERFACE";
            default: return "STD_UNKNOWN";
        }
    }

    if ((bm_request_type & 0x60U) == 0x20U)
    {
        switch (request)
        {
            case 0x20U: return "SET_LINE_CODING";
            case 0x21U: return "GET_LINE_CODING";
            case 0x22U: return "SET_CONTROL_LINE_STATE";
            case 0x23U: return "SEND_BREAK";
            default: return "CLASS_UNKNOWN";
        }
    }

    return "TYPE_UNKNOWN";
}

static const char *usb_trace_state_name(uint16_t state)
{
    switch (state)
    {
        case USB_DBG_STATE_GET_DESCRIPTOR: return "GET_DESCRIPTOR";
        case USB_DBG_STATE_SET_ADDRESS: return "SET_ADDRESS";
        case USB_DBG_STATE_SET_CONFIGURATION: return "SET_CONFIGURATION";
        case USB_DBG_STATE_SET_INTERFACE: return "SET_INTERFACE";
        case USB_DBG_STATE_SET_LINE_CODING_PENDING: return "SET_LINE_CODING_PENDING";
        case USB_DBG_STATE_GET_LINE_CODING: return "GET_LINE_CODING";
        case USB_DBG_STATE_SET_CONTROL_LINE_STATE: return "SET_CONTROL_LINE_STATE";
        case USB_DBG_STATE_SEND_BREAK: return "SEND_BREAK";
        case USB_DBG_STATE_SW_CONFIGURED: return "SW_CONFIGURED";
        case USB_DBG_STATE_STATUS_STAGE: return "STATUS_STAGE";
        case USB_DBG_STATE_PULLUP_ENABLED: return "PULLUP_ENABLED";
        case USB_DBG_STATE_BUS_RESET_REARM: return "BUS_RESET_REARM";
        case USB_DBG_STATE_PULLUP_DISABLED: return "PULLUP_DISABLED";
        case USB_DBG_STATE_CTRL_IN_PACKET: return "CTRL_IN_PACKET";
        default: return "STATE_UNKNOWN";
    }
}

static const char *usb_trace_stall_name(uint16_t reason)
{
    switch (reason)
    {
        case USB_DBG_STALL_UNSUPPORTED_STANDARD: return "UNSUPPORTED_STANDARD";
        case USB_DBG_STALL_UNSUPPORTED_CLASS: return "UNSUPPORTED_CLASS";
        case USB_DBG_STALL_UNSUPPORTED_TYPE: return "UNSUPPORTED_TYPE";
        case USB_DBG_STALL_CONTROL_SEQUENCE: return "CONTROL_SEQUENCE";
        case USB_DBG_STALL_SET_LINE_CODING_DATA: return "SET_LINE_CODING_DATA";
        default: return "STALL_UNKNOWN";
    }
}

static uint8_t usb_trace_should_print(uint8_t configured, const usb_debug_trace_t *trace)
{
    if (configured == 0U)
    {
        return 1U;
    }

    if ((trace->event == USB_DBG_EVT_STALL) || (trace->event == USB_DBG_EVT_ERROR))
    {
        return 1U;
    }

    return 0U;
}

static void usb_cdc_drain_debug_trace(uint8_t configured)
{
    usb_debug_trace_t trace;
    static uint16_t s_last_dropped = 0U;

    while (USB_Debug_PopTrace(&trace) != 0U)
    {
        if (usb_trace_should_print(configured, &trace) == 0U)
        {
            continue;
        }

        switch (trace.event)
        {
            case USB_DBG_EVT_INIT:
                debug_print("[USB CDC TRACE] init mode=%u SYSCFG=0x%x INTENB0=0x%x BRDYENB=0x%x\r\n",
                            (unsigned)trace.param0,
                            (unsigned)trace.param1,
                            (unsigned)trace.param2,
                            (unsigned)trace.param3);
                break;

            case USB_DBG_EVT_DVST:
                debug_print("[USB CDC TRACE] DVST is0=0x%x SYSSTS0=0x%x DVSTCTR0=0x%x addr=%u cfg=%u\r\n",
                            (unsigned)trace.param0,
                            (unsigned)trace.param1,
                            (unsigned)trace.param2,
                            (unsigned)(trace.param3 & 0x00FFU),
                            (unsigned)((trace.param3 >> 8) & 0x00FFU));
                break;

            case USB_DBG_EVT_CTRT:
                debug_print("[USB CDC TRACE] CTRT is0=0x%x intsts0=0x%x CTSQ=%s(%u) pending=%u\r\n",
                            (unsigned)trace.param0,
                            (unsigned)trace.param1,
                            usb_trace_ctsq_name(trace.param2),
                            (unsigned)trace.param2,
                            (unsigned)trace.param3);
                break;

            case USB_DBG_EVT_SETUP:
                debug_print("[USB CDC TRACE] SETUP %s bmReq=0x%x bReq=0x%x wValue=0x%x wIndex=0x%x wLen=%u\r\n",
                            usb_trace_request_name(trace.param0),
                            (unsigned)(trace.param0 & 0x00FFU),
                            (unsigned)((trace.param0 >> 8) & 0x00FFU),
                            (unsigned)trace.param1,
                            (unsigned)trace.param2,
                            (unsigned)trace.param3);
                break;

            case USB_DBG_EVT_STATE:
                if (trace.param0 == USB_DBG_STATE_STATUS_STAGE)
                {
                    debug_print("[USB CDC TRACE] state=%s CTSQ=%s(%u) DCPCTR=0x%x\r\n",
                                usb_trace_state_name(trace.param0),
                                usb_trace_ctsq_name(trace.param1),
                                (unsigned)trace.param1,
                                (unsigned)trace.param2);
                }
                else
                {
                    debug_print("[USB CDC TRACE] state=%s v0=0x%x v1=0x%x v2=0x%x\r\n",
                                usb_trace_state_name(trace.param0),
                                (unsigned)trace.param1,
                                (unsigned)trace.param2,
                                (unsigned)trace.param3);
                }
                break;

            case USB_DBG_EVT_LINE_CODING:
            {
                uint32_t baudrate = (uint32_t)trace.param0 | ((uint32_t)trace.param1 << 16);
                debug_print("[USB CDC TRACE] line_coding baud=%u stop=%u parity=%u data=%u\r\n",
                            (unsigned)baudrate,
                            (unsigned)((trace.param2 >> 8) & 0x00FFU),
                            (unsigned)(trace.param2 & 0x00FFU),
                            (unsigned)trace.param3);
                break;
            }

            case USB_DBG_EVT_STALL:
                debug_print("[USB CDC TRACE] STALL reason=%s req=%s DCPCTR=0x%x INTSTS0=0x%x\r\n",
                            usb_trace_stall_name(trace.param0),
                            usb_trace_request_name(trace.param1),
                            (unsigned)trace.param2,
                            (unsigned)trace.param3);
                break;

            case USB_DBG_EVT_ERROR:
                debug_print("[USB CDC TRACE] error=%u arg0=0x%x arg1=0x%x arg2=0x%x\r\n",
                            (unsigned)trace.param0,
                            (unsigned)trace.param1,
                            (unsigned)trace.param2,
                            (unsigned)trace.param3);
                break;

            default:
                debug_print("[USB CDC TRACE] unknown event=%u p0=0x%x p1=0x%x p2=0x%x p3=0x%x\r\n",
                            (unsigned)trace.event,
                            (unsigned)trace.param0,
                            (unsigned)trace.param1,
                            (unsigned)trace.param2,
                            (unsigned)trace.param3);
                break;
        }
    }

    if (USB_Debug_GetDroppedTraceCount() != s_last_dropped)
    {
        s_last_dropped = USB_Debug_GetDroppedTraceCount();
        debug_print("[USB CDC TRACE] dropped=%u\r\n", (unsigned)s_last_dropped);
    }
}

static void usb_hex_dump(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0U; i < len; i++)
    {
        debug_print("[USB HOST TEST] desc[%u] = 0x%x\r\n",
                    (unsigned)i,
                    (unsigned)data[i]);
    }
}

static void task_usb_cdc_logger(void *arg)
{
    uint32_t elapsed_ms = 0U;
    uint32_t waiting_log_ms = 0U;
    uint32_t seq = 0U;
    uint8_t last_configured = 0U;
    uint8_t last_host_ready = 0U;
    drv_status_t init_st;
    (void)arg;

    init_st = USB_Init(USB_MODE_DEVICE_CDC);
    if (init_st != DRV_OK)
    {
        debug_print("[USB CDC TEST] USB_Init failed (%u)\r\n", (unsigned)init_st);
        for (;;)
        {
            OS_Task_Delay(OS_WAIT_FOREVER);
        }
    }

    debug_print("[USB CDC TEST] Device mode active. Connect USB FS to host.\r\n");

    for (;;)
    {
        uint8_t configured;
        uint8_t host_ready;

        USB_PollEvents();
        configured = USB_Dev_IsConfigured();
        host_ready = USB_Dev_IsHostReady();
        usb_cdc_drain_debug_trace(configured);

        if (configured != last_configured)
        {
            debug_print("[USB CDC TEST] configured=%u\r\n", (unsigned)configured);
            last_configured = configured;
        }

        if (host_ready != last_host_ready)
        {
            debug_print("[USB CDC TEST] host_ready=%u\r\n", (unsigned)host_ready);
            last_host_ready = host_ready;
        }

        if ((host_ready != 0U) && (elapsed_ms >= USB_CDC_LOG_PERIOD_MS))
        {
            drv_status_t st = USB_Dev_Printf(
                "[USB CDC TEST] seq=%u tick=%u host-ready\r\n",
                (unsigned)seq,
                (unsigned)OS_GetTick());
            debug_print("[USB CDC TEST] TX %s seq=%u\r\n",
                        (st == DRV_OK) ? "OK " : "ERR",
                        (unsigned)seq);

            seq++;
            elapsed_ms = 0U;
        }

        if ((configured != 0U) && (host_ready == 0U) && (waiting_log_ms >= 2000U))
        {
            debug_print("[USB CDC TEST] waiting host open (SET_CONTROL_LINE_STATE DTR=1)\r\n");
            waiting_log_ms = 0U;
        }

        OS_Task_Delay(USB_CDC_POLL_PERIOD_MS);
        elapsed_ms += USB_CDC_POLL_PERIOD_MS;
        waiting_log_ms += USB_CDC_POLL_PERIOD_MS;
    }
}

static void task_usb_host_descriptor_test(void *arg)
{
    const usb_host_cdc_device_t *dev;
    const usb_host_descriptor_snapshot_t *snap;
    drv_status_t st;
    uint16_t vid;
    uint16_t pid;
    (void)arg;

    OS_Task_Delay(USB_HOST_BOOT_DELAY_MS);

    debug_print("[USB HOST TEST] Host mode init...\r\n");
    st = USB_Init(USB_MODE_HOST_CDC_ACM);
    if (st != DRV_OK)
    {
        debug_print("[USB HOST TEST] USB_Init failed (%u)\r\n", (unsigned)st);
        for (;;)
        {
            OS_Task_Delay(OS_WAIT_FOREVER);
        }
    }

    debug_print("[USB HOST TEST] Waiting SIM module attach and enumeration...\r\n");
    st = USB_Host_Enumerate();
    if (st != DRV_OK)
    {
        debug_print("[USB HOST TEST] Enumeration failed (%u)\r\n", (unsigned)st);
        for (;;)
        {
            OS_Task_Delay(OS_WAIT_FOREVER);
        }
    }

    dev = USB_Host_GetDeviceInfo();
    snap = USB_Host_GetDescriptorSnapshot();

    vid = (uint16_t)snap->device_desc[8] | ((uint16_t)snap->device_desc[9] << 8);
    pid = (uint16_t)snap->device_desc[10] | ((uint16_t)snap->device_desc[11] << 8);

    debug_print("[USB HOST TEST] Enumeration PASS\r\n");
    debug_print("[USB HOST TEST] VID=0x%x PID=0x%x class=0x%x sub=0x%x proto=0x%x\r\n",
                (unsigned)vid,
                (unsigned)pid,
                (unsigned)snap->device_desc[4],
                (unsigned)snap->device_desc[5],
                (unsigned)snap->device_desc[6]);
    debug_print("[USB HOST TEST] EP0_MPS=%u cfg_len=%u ifaces=%u cfg=%u\r\n",
                (unsigned)snap->max_packet_size_ep0,
                (unsigned)snap->config_total_length,
                (unsigned)snap->num_interfaces,
                (unsigned)snap->configuration_value);
    debug_print("[USB HOST TEST] BULK_OUT=EP%u/%u BULK_IN=EP%u/%u addr=%u\r\n",
                (unsigned)dev->bulk_out_ep,
                (unsigned)dev->bulk_out_mps,
                (unsigned)dev->bulk_in_ep,
                (unsigned)dev->bulk_in_mps,
                (unsigned)dev->address);

    usb_hex_dump(snap->device_desc, (uint32_t)sizeof(snap->device_desc));

    for (;;)
    {
        OS_Task_Delay(OS_WAIT_FOREVER);
    }
}

void test_usb_cdc_logger_init(void)
{
    (void)OS_Task_Create(&s_usb_cdc_tcb,
                         task_usb_cdc_logger,
                         (void *)0,
                         USB_TEST_TASK_PRIO,
                         "usb_cdc");
}

void test_usb_host_descriptor_init(void)
{
    (void)OS_Task_Create(&s_usb_host_tcb,
                         task_usb_host_descriptor_test,
                         (void *)0,
                         USB_TEST_TASK_PRIO,
                         "usb_host");
}
