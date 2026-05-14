#include "server_comm.h"

#include "debug_print.h"
#include "drv_usb.h"
#include "drv_uart.h"
#include "fwupdate_receiver.h"
#include "iaq_predictor.h"
#include "kernel.h"
#include "rtos_config.h"
#include "sensor_simulator.h"
#include "semaphore.h"

#include <stdbool.h>
#include <stdint.h>

#define SERVER_COMM_TASK_PRIO_TX  5U
#define SERVER_COMM_TASK_PRIO_RX  6U
#define SERVER_COMM_TASK_PRIO_IAQ 7U

#define IAQ_STARTUP_DELAY_MS      3000U
#define IAQ_STARTUP_POLL_MS         10U
#define IAQ_CYCLE_DELAY_MS        5000U

typedef struct {
    int32_t tvoc_x10;
    int32_t eco2_x10;
    int32_t iaq_x100;
    uint8_t raw_pending;
    uint8_t iaq_pending;
} server_comm_ctx_t;

static OS_TCB_t s_server_comm_tx_tcb;
static OS_TCB_t s_server_comm_rx_tcb;
static OS_TCB_t s_server_comm_iaq_tcb;
static Semaphore_t s_server_comm_tx_sem;
static server_comm_ctx_t s_server_comm;
static uint8_t s_server_comm_inited = 0U;

static volatile uint8_t s_ota_active = 0U;

static int32_t server_comm_round_to_i32(float val)
{
    if (val >= 0.0f)
    {
        return (int32_t)(val + 0.5f);
    }

    return (int32_t)(val - 0.5f);
}

static void server_comm_uart_init(void)
{
#if OS_DEBUG_BACKEND_USB_CDC
    /* debug_print() uses USB CDC in this build, so the SCI channel used for
     * Arduino must be brought up explicitly here. */
    UART_Init((UART_t)OS_DEBUG_UART_CHANNEL, OS_DEBUG_UART_BAUDRATE);
#endif
}

static void server_comm_send_u8(uint8_t b)
{
    UART_SendChar((UART_t)OS_DEBUG_UART_CHANNEL, (char)b);
}

static void server_comm_send_cstr(const char *text)
{
    if (text == (const char *)0)
    {
        return;
    }

    while (*text != '\0')
    {
        server_comm_send_u8((uint8_t)*text++);
    }
}

static void server_comm_send_u32(uint32_t value)
{
    char digits[10];
    uint8_t idx = 0U;

    if (value == 0U)
    {
        server_comm_send_u8((uint8_t)'0');
        return;
    }

    while ((value > 0U) && (idx < (uint8_t)sizeof(digits)))
    {
        digits[idx++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (idx > 0U)
    {
        server_comm_send_u8((uint8_t)digits[--idx]);
    }
}

static uint32_t server_comm_abs_i32(int32_t value)
{
    if (value < 0)
    {
        return (uint32_t)(-value);
    }

    return (uint32_t)value;
}

static void server_comm_send_fixed_1(int32_t value_x10)
{
    uint32_t abs_value = server_comm_abs_i32(value_x10);

    if (value_x10 < 0)
    {
        server_comm_send_u8((uint8_t)'-');
    }

    server_comm_send_u32(abs_value / 10U);
    server_comm_send_u8((uint8_t)'.');
    server_comm_send_u8((uint8_t)('0' + (abs_value % 10U)));
}

static void server_comm_send_fixed_2(int32_t value_x100)
{
    uint32_t abs_value = server_comm_abs_i32(value_x100);

    if (value_x100 < 0)
    {
        server_comm_send_u8((uint8_t)'-');
    }

    server_comm_send_u32(abs_value / 100U);
    server_comm_send_u8((uint8_t)'.');
    server_comm_send_u8((uint8_t)('0' + ((abs_value / 10U) % 10U)));
    server_comm_send_u8((uint8_t)('0' + (abs_value % 10U)));
}

static void server_comm_send_raw(int32_t tvoc_x10, int32_t eco2_x10)
{
    server_comm_send_cstr("Raw: TVOC=");
    server_comm_send_fixed_1(tvoc_x10);
    server_comm_send_cstr("ppb | eCO2=");
    server_comm_send_fixed_1(eco2_x10);
    server_comm_send_cstr("ppm\r\n");
}

static void server_comm_send_iaq(int32_t iaq_x100)
{
    server_comm_send_cstr("Predict=");
    server_comm_send_fixed_2(iaq_x100);
    server_comm_send_cstr("\r\n");
}

static void server_comm_send_published(int32_t tvoc_x10, int32_t actual_x100, int32_t predict_x100)
{
    server_comm_send_cstr("Published: TVOC=");
    server_comm_send_fixed_1(tvoc_x10);
    server_comm_send_cstr("ppb | Actual=");
    server_comm_send_fixed_2(actual_x100);
    server_comm_send_cstr(" | Predict=");
    server_comm_send_fixed_2(predict_x100);
    server_comm_send_cstr("\r\n");
}

static void server_comm_send_env(int32_t temp_x10, int32_t hum_x10)
{
    server_comm_send_cstr("T=");
    server_comm_send_fixed_1(temp_x10);
    server_comm_send_cstr(" C  RH=");
    server_comm_send_fixed_1(hum_x10);
    server_comm_send_cstr("%\r\n");
}

static void task_server_comm_tx(void *arg)
{
    int32_t tvoc_x10;
    int32_t eco2_x10;
    int32_t iaq_x100;
    uint8_t raw_pending;
    uint8_t iaq_pending;

    (void)arg;

    for (;;)
    {
        (void)OS_SemPend(&s_server_comm_tx_sem, OS_WAIT_FOREVER);

        OS_EnterCritical();
        tvoc_x10 = s_server_comm.tvoc_x10;
        eco2_x10 = s_server_comm.eco2_x10;
        iaq_x100 = s_server_comm.iaq_x100;
        raw_pending = s_server_comm.raw_pending;
        iaq_pending = s_server_comm.iaq_pending;
        s_server_comm.raw_pending = 0U;
        s_server_comm.iaq_pending = 0U;
        OS_ExitCritical();

        if (raw_pending != 0U)
        {
            server_comm_send_raw(tvoc_x10, eco2_x10);
        }

        if (iaq_pending != 0U)
        {
            server_comm_send_iaq(iaq_x100);
        }
    }
}

static void task_server_comm_rx(void *arg)
{
    (void)arg;
    static uint32_t s_fer_count  = 0U;
    static uint32_t s_orer_count = 0U;
    static uint32_t s_rdrf_count = 0U;  /* good bytes seen by fwupdate */
    static uint32_t s_log_timer  = 0U;

    for (;;)
    {
        /* Clear UART hardware errors (ORER/FER/PER) using correct RA6M5 sequence */
        {
            uint8_t ssr = SCI_SSR(OS_DEBUG_UART_CHANNEL);

            if ((ssr & SSR_RDRF) != 0U)
            {
                s_rdrf_count++;
            }

            if ((ssr & (SSR_ORER | SSR_FER | SSR_PER)) != 0U)
            {
                if ((ssr & SSR_FER)  != 0U) { s_fer_count++;  }
                if ((ssr & SSR_ORER) != 0U) { s_orer_count++; }

                if ((ssr & SSR_RDRF) != 0U)
                {
                    (void)SCI_RDR(OS_DEBUG_UART_CHANNEL);
                }
                SCI_SSR(OS_DEBUG_UART_CHANNEL) = (uint8_t)0xC0U;
            }
        }

        /* Periodic UART health report every 5000 ms */
        s_log_timer++;
        if (s_log_timer >= 5000U)
        {
            s_log_timer = 0U;
            debug_print("[srv_rx] UART health: RDRF=%lu FER=%lu ORER=%lu\r\n",
                        s_rdrf_count, s_fer_count, s_orer_count);
            s_rdrf_count = 0U;
            s_fer_count  = 0U;
            s_orer_count = 0U;
        }

        fwupdate_state_t fw_state = fwupdate_receiver_run();

        /* Signal IAQ TX task to pause when an OTA transfer is active. */
        s_ota_active = ((fw_state != FWUPDATE_STATE_IDLE) &&
                        (fw_state != FWUPDATE_STATE_DONE)) ? 1U : 0U;

        OS_Task_Delay(1U);
    }
}



static void task_server_comm_iaq(void *arg)
{
    uint32_t startup_wait_ms = 0U;
    (void)arg;

#if OS_DEBUG_BACKEND_USB_CDC
    while ((startup_wait_ms < IAQ_STARTUP_DELAY_MS) && (USB_Dev_IsConfigured() == 0U))
    {
        OS_Task_Delay(IAQ_STARTUP_POLL_MS);
        startup_wait_ms += IAQ_STARTUP_POLL_MS;
    }
#endif

    debug_print("\r\n[IAQ Task] Starting TFLite initialization...\r\n");

    if (!IAQ_Init())
    {
        debug_print("FATAL: IAQ_Init() failed. Task halted.\r\n");
        for (;;)
        {
            OS_Task_Delay(5000U);
        }
    }

    debug_print("[IAQ Task] Init OK. Starting 5s forecast loop...\r\n");
    SensorSim_Init();
    debug_print("[SensorSim_Init OK]\r\n");

    for (;;)
    {
        SensorPacket_t pkt = SensorSim_Read();
        float forecast = IAQ_Predict(pkt.tvoc);

        int32_t tvoc_10 = (int32_t)(pkt.tvoc * 10.0f);
        int32_t actual_100 = (int32_t)(pkt.iaq_reference * 100.0f);
        int32_t predict_100 = (int32_t)(forecast * 100.0f);

        int t_int = (int)(tvoc_10 / 10);
        int t_frac = (int)(tvoc_10 % 10);

        int a_int = (int)(actual_100 / 100);
        int a_frac1 = (int)((actual_100 % 100) / 10);
        int a_frac2 = (int)(actual_100 % 10);

        int p_int = (int)(predict_100 / 100);
        int p_frac1 = (int)((predict_100 % 100) / 10);
        int p_frac2 = (int)(predict_100 % 10);

        int32_t temp_10 = (int32_t)(pkt.temperature * 10.0f);
        int32_t hum_10 = (int32_t)(pkt.humidity * 10.0f);
        int temp_int = (int)(temp_10 / 10);
        int temp_frac = (int)(temp_10 % 10);
        int hum_int = (int)(hum_10 / 10);
        int hum_frac = (int)(hum_10 % 10);

        debug_print("[SensorSim_Read OK]\r\n");
        debug_print("[IAQ_Predict OK]\r\n");
        debug_print("Published: TVOC=%d.%dppb | Actual=%d.%d%d | Predict=%d.%d%d\r\n",
                    t_int, t_frac, a_int, a_frac1, a_frac2, p_int, p_frac1, p_frac2);
        debug_print("T=%d.%d C  RH=%d.%d%%\r\n", temp_int, temp_frac, hum_int, hum_frac);

        /* Pause UART TX to ESP32 while OTA transfer is receiving frames. */
        if (s_ota_active == 0U)
        {
            server_comm_send_published(tvoc_10, actual_100, predict_100);
            server_comm_send_env(temp_10, hum_10);
        }
        else
        {
            debug_print("[srv_iaq] TX paused: OTA in progress\r\n");
        }

        OS_Task_Delay(IAQ_CYCLE_DELAY_MS);
    }
}

void server_comm_init(void)
{
    if (s_server_comm_inited != 0U)
    {
        return;
    }

    s_server_comm.tvoc_x10 = 0;
    s_server_comm.eco2_x10 = 0;
    s_server_comm.iaq_x100 = 0;
    s_server_comm.raw_pending = 0U;
    s_server_comm.iaq_pending = 0U;

    server_comm_uart_init();
    fwupdate_receiver_init();

    if (OS_SemCreate(&s_server_comm_tx_sem, 0U, 1U) != OS_OK)
    {
        return;
    }

    if (OS_Task_Create(&s_server_comm_tx_tcb,
                       task_server_comm_tx,
                       (void *)0,
                       SERVER_COMM_TASK_PRIO_TX,
                       "srv_tx") != OS_OK)
    {
        return;
    }

    if (OS_Task_Create(&s_server_comm_rx_tcb,
                       task_server_comm_rx,
                       (void *)0,
                       SERVER_COMM_TASK_PRIO_RX,
                       "srv_rx") != OS_OK)
    {
        return;
    }

    if (OS_Task_Create(&s_server_comm_iaq_tcb,
                       task_server_comm_iaq,
                       (void *)0,
                       SERVER_COMM_TASK_PRIO_IAQ,
                       "srv_iaq") != OS_OK)
    {
        return;
    }

    s_server_comm_inited = 1U;
}

void server_comm_publish_raw(float tvoc_ppb, float eco2_ppm)
{
    if (s_server_comm_inited == 0U)
    {
        return;
    }

    OS_EnterCritical();
    s_server_comm.tvoc_x10 = server_comm_round_to_i32(tvoc_ppb * 10.0f);
    s_server_comm.eco2_x10 = server_comm_round_to_i32(eco2_ppm * 10.0f);
    s_server_comm.raw_pending = 1U;
    OS_ExitCritical();

    (void)OS_SemPost(&s_server_comm_tx_sem);
}

void server_comm_publish_predict(float iaq_predict)
{
    if (s_server_comm_inited == 0U)
    {
        return;
    }

    OS_EnterCritical();
    s_server_comm.iaq_x100 = server_comm_round_to_i32(iaq_predict * 100.0f);
    s_server_comm.iaq_pending = 1U;
    OS_ExitCritical();

    (void)OS_SemPost(&s_server_comm_tx_sem);
}
