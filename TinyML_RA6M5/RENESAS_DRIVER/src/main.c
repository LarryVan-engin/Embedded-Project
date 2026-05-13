/**
 * @file    main.c
 * @brief   RTOS application test for scheduler and software timer services.
 *
 * Tasks:
 *   1. LED1 blink task            → toggles via OS_Task_Delay()
 *   2. LED2 timer task            → wakes on semaphore posted by software timer
 */

#include "GPIO.h"
#include "debug_print.h"
#include "drv_clk.h"
#include "drv_i2c.h"
#include "drv_uart.h"
#include "drv_usb.h"
#include "board_config.h"
#include "kernel.h"
#include "rtos_config.h"
#include "server_comm.h"
#include "semaphore.h"
#include "software_timer.h"
#include "test/app_test_config.h"
#include "test/test_flash_nvs.h"
#include "test/test_rtos.h"
#include "test/test_usb.h"
#include "safe_reset.h"
#include <stdint.h>

/* Board-specific LED definitions from board_config.h */
#define LED1_PORT_NUM LED1_PORT
#define LED2_PORT_NUM LED2_PORT
#define LED3_PORT_NUM LED3_PORT

#define TASK_PRIO_LED_TIMER 3U
#define TASK_PRIO_LED_BLINK 4U
#define TASK_PRIO_USB_SVC   2U

/* LED macros now use individual port definitions */
#define LED1_ON()  GPIO_Write_Pin((GPIO_PORT_t)LED1_PORT_NUM, LED1_PIN, GPIO_PIN_SET)
#define LED2_ON()  GPIO_Write_Pin((GPIO_PORT_t)LED2_PORT_NUM, LED2_PIN, GPIO_PIN_SET)
#define LED3_ON()  GPIO_Write_Pin((GPIO_PORT_t)LED3_PORT_NUM, LED3_PIN, GPIO_PIN_SET)
#define LED1_OFF() GPIO_Write_Pin((GPIO_PORT_t)LED1_PORT_NUM, LED1_PIN, GPIO_PIN_RESET)
#define LED2_OFF() GPIO_Write_Pin((GPIO_PORT_t)LED2_PORT_NUM, LED2_PIN, GPIO_PIN_RESET)
#define LED3_OFF() GPIO_Write_Pin((GPIO_PORT_t)LED3_PORT_NUM, LED3_PIN, GPIO_PIN_RESET)

static OS_TCB_t g_led_blink_tcb;
static OS_TCB_t g_timer_led_tcb;
#if OS_DEBUG_BACKEND_USB_CDC
static OS_TCB_t g_usb_svc_tcb;
#endif
static Semaphore_t g_led_timer_sem;
static Timer_t g_led_timer;

static void led_init(void) {
  GPIO_Config((GPIO_PORT_t)LED1_PORT_NUM, LED1_PIN, GPIO_CNF_OUT_PP, GPIO_MODE_OUTPUT);
  GPIO_Config((GPIO_PORT_t)LED2_PORT_NUM, LED2_PIN, GPIO_CNF_OUT_PP, GPIO_MODE_OUTPUT);
  GPIO_Config((GPIO_PORT_t)LED3_PORT_NUM, LED3_PIN, GPIO_CNF_OUT_PP, GPIO_MODE_OUTPUT);
  LED1_OFF();
  LED2_OFF();
  LED3_OFF();
}

static void led_toggle(uint8_t pin) {
  /* Determine which port this pin belongs to based on board config */
  GPIO_PORT_t port;
  
  if (pin == LED1_PIN) {
    port = (GPIO_PORT_t)LED1_PORT_NUM;
  } else if (pin == LED2_PIN) {
    port = (GPIO_PORT_t)LED2_PORT_NUM;
  } else if (pin == LED3_PIN) {
    port = (GPIO_PORT_t)LED3_PORT_NUM;
  } else {
    return;  /* Unknown pin */
  }
  
  uint8_t cur = GPIO_Read_Pin(port, pin);
  GPIO_Write_Pin(port, pin, (cur != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void delay_ms_bm(uint32_t ms) {
  volatile uint32_t n = ms * 100000U;
  while (n-- != 0U) {
    __asm volatile("nop");
  }
}

static void app_panic(const char *step, int32_t err) {
  debug_print("FATAL: %s failed (%d)\r\n", step, (int)err);

  for (;;) {
    led_toggle(LED3_PIN);
    delay_ms_bm(120U);
  }
}

static void i2c_scan(I2C_t i2c) {
  uint8_t count = 0U;

  debug_print("I2C scan (0x08-0x77):\r\n");

  for (uint8_t addr = 0x08U; addr <= 0x77U; addr++) {
    I2C_Start(i2c);
    uint8_t ack = I2C_Transmit_Address(i2c, addr, I2C_WRITE);
    I2C_Stop(i2c);

    if (ack != 0U) {
      debug_print("  [0x%x] ACK\r\n", (unsigned)addr);
      count++;
    }
  }

  if (count == 0U) {
    debug_print("  No devices found.\r\n");
    debug_print("  Check pull-ups, power, and SCL/SDA routing.\r\n");
  }

  debug_print("\r\n");
}

static void timer_signal_led_task(void *arg) {
  (void)arg;
  (void)OS_SemPost(&g_led_timer_sem);
}

static void task_led_blink(void *arg) {
  (void)arg;

  for (;;) {
    led_toggle(LED1_PIN);
    OS_Task_Delay(250U);

    /* Periodic stack canary check — triggers safe_reset on overflow.
     * Runs every 250 ms alongside the LED blink duty cycle. */
    safe_reset_check_stacks();
  }
}

static void task_timer_led(void *arg) {
  uint32_t wake_count = 0U;
  (void)arg;

  for (;;) {
    (void)OS_SemPend(&g_led_timer_sem, OS_WAIT_FOREVER);
    led_toggle(LED2_PIN);
    wake_count++;

    if ((wake_count % 10U) == 0U) {
      debug_print("[timer] LED2 toggles=%u\r\n", (unsigned)wake_count);
    }
  }
}

#if OS_DEBUG_BACKEND_USB_CDC
static void task_usb_service(void *arg) {
  (void)arg;

  for (;;) {
    USB_PollEvents();
    OS_Task_Delay(1U);
  }
}
#endif

int main(void) {
  int32_t status;
  uint8_t dbg_link_ok;
  uint32_t crash_reason;

  led_init();

  debug_print_init();

  /*
   * Safe-reset init — MUST run before any task code.
   *
   * Checks the NVS crash record written by a previous safe_reset_trigger().
   * If a record exists, logs the reason, clears the marker (prevents loop),
   * and returns the reason code.  On a clean first boot returns NONE.
   *
   * When SAFE_RESET_SELF_TEST is enabled (see below) the first clean boot
   * writes an APP_REQUEST crash record and immediately resets the MCU.
   * The second boot finds + clears the record, logs success, and continues
   * normally — proving the full NVS persist→clear path works end-to-end.
   */
  crash_reason = safe_reset_init();
debug_print("First StartUp!\r\n");
#if SAFE_RESET_SELF_TEST
  if (crash_reason == SAFE_RESET_REASON_NONE)
  {
      /* First boot — deliberately trigger a crash record to NVS */
      debug_print("[SAFE_RESET SELF_TEST] Boot 1: writing test record and resetting...\r\n");
      safe_reset_trigger(SAFE_RESET_REASON_APP_REQUEST);   /* never returns */
  }
  else
  {
      /* Second boot — record was found and cleared by safe_reset_init() */
      debug_print("[SAFE_RESET SELF_TEST] Boot 2 OK: recovered from reason=0x%08lx\r\n",
                  (unsigned long)crash_reason);
  }
#else
  (void)crash_reason;
#endif /* SAFE_RESET_SELF_TEST */
  
  debug_print("\r\n=== RA6M5 RTOS Application Test ===\r\n");
#if OS_DEBUG_BACKEND_USB_CDC
  debug_print("DEBUG : USB CDC (Device FS)\r\n");
#else
  debug_print("BOARD : %s\r\n", IS_BOARD_CK() ? "CK-RA6M5" : "EK-RA6M5");
  debug_print("UART  : SCI%u @ %u baud\r\n",
              (unsigned)DEBUG_UART_CHANNEL,
              (unsigned)DEBUG_UART_BAUDRATE);
#endif
  debug_print("CLK   : ICLK=%u Hz, SCI_CLK=%u Hz\r\n",
              (unsigned)CLK_GetActualICLK(),
              (unsigned)CLK_GetActualSCIClock());
  debug_print("UARTCFG: BRR_DIV=%u\r\n", (unsigned)DEBUG_UART_BRR_DIV);
  debug_print("I2C   : RIIC1 P512(SCL)/P511(SDA) @ 100 kHz\r\n");
  debug_print("LINK  : %s\r\n", dbg_link_ok != 0U ? "OK" : "FAIL");

  I2C_Init(I2C1, 50U, I2C_SPEED_STANDARD);
  i2c_scan(I2C1);
  delay_ms_bm(120U);

  OS_Init();

#if PREEMTIVE_RTOS_TEST
  /* Init RTOS Preemptive Test Task */
  test_rtos_preemptive_init();
#endif

#if OS_DEBUG_BACKEND_USB_CDC
  status = OS_Task_Create(&g_usb_svc_tcb, task_usb_service, (void *)0,
                          TASK_PRIO_USB_SVC, "usb_svc");
  if (status != OS_OK) {
    app_panic("OS_Task_Create usb_svc", status);
  }
#endif

#if (OS_USB_TEST_MODE == OS_USB_TEST_MODE_DEVICE_CDC)
  /* test_usb_cdc_logger: calls USB_Init + USB_PollEvents internally.
   * Drains USB trace ring buffer to UART and sends periodic test
   * messages via USB_Dev_Printf (blocking, waits for BEMP). */
  test_usb_cdc_logger_init();
#endif

  server_comm_init();

#if OS_FLASH_NVS_TEST_ENABLE != 0U
  debug_print("[FLASH NVS TEST] Mode: scratch-block self-test\r\n");
  test_flash_nvs_init();
#else
  debug_print("[FLASH NVS TEST] Mode: disabled\r\n");
#endif

  status = OS_SemCreate(&g_led_timer_sem, 0U, 1U);
  if (status != OS_OK) {
    app_panic("OS_SemCreate", status);
  }

  status = OS_TimerCreate(&g_led_timer, timer_signal_led_task, (void *)0, 500U,
                          OS_TIMER_AUTO_RELOAD);
  if (status != OS_OK) {
    app_panic("OS_TimerCreate", status);
  }

  status = OS_Task_Create(&g_led_blink_tcb, task_led_blink, (void *)0,
                          TASK_PRIO_LED_BLINK, "led_blink");
  if (status != OS_OK) {
    app_panic("OS_Task_Create led_blink", status);
  }

  status = OS_Task_Create(&g_timer_led_tcb, task_timer_led, (void *)0,
                          TASK_PRIO_LED_TIMER, "led_timer");
  if (status != OS_OK) {
    app_panic("OS_Task_Create led_timer", status);
  }

  status = OS_TimerStart(&g_led_timer);
  if (status != OS_OK) {
    app_panic("OS_TimerStart", status);
  }

  debug_print("Tasks ready: LED1 blink, LED2 timer blink\r\n");
  LED3_ON();

  OS_Start();

  for (;;) {
    __asm volatile("nop");
  }
}
