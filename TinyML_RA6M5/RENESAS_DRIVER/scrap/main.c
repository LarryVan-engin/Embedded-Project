/**
 * @file    main.c
 * @brief   RTOS application test for scheduler, software timer, and AHT20.
 *
 * Tasks:
 *   1. LED1 blink task            → toggles via OS_Task_Delay()
 *   2. Sensor logger task         → reads AHT20 and prints samples
 *   3. LED2 timer task            → wakes on semaphore posted by software timer
 */

#include "GPIO.h"
#include "bsp_aht20.h"
#include "debug_print.h"
#include "drv_i2c.h"
#include "drv_uart.h"
#include "kernel.h"
#include "rtos_config.h"
#include "semaphore.h"
#include "software_timer.h"
#include <stdint.h>

#define LED_PORT GPIO_PORT0
#define LED1_PIN 6U
#define LED2_PIN 7U
#define LED3_PIN 8U

#define TASK_PRIO_SENSOR 2U
#define TASK_PRIO_LED_TIMER 3U
#define TASK_PRIO_LED_BLINK 4U

#define LED1_ON() GPIO_Write_Pin(LED_PORT, LED1_PIN, GPIO_PIN_SET)
#define LED2_ON() GPIO_Write_Pin(LED_PORT, LED2_PIN, GPIO_PIN_SET)
#define LED3_ON() GPIO_Write_Pin(LED_PORT, LED3_PIN, GPIO_PIN_SET)
#define LED1_OFF() GPIO_Write_Pin(LED_PORT, LED1_PIN, GPIO_PIN_RESET)
#define LED2_OFF() GPIO_Write_Pin(LED_PORT, LED2_PIN, GPIO_PIN_RESET)
#define LED3_OFF() GPIO_Write_Pin(LED_PORT, LED3_PIN, GPIO_PIN_RESET)

static OS_TCB_t g_led_blink_tcb;
static OS_TCB_t g_sensor_tcb;
static OS_TCB_t g_timer_led_tcb;
static Semaphore_t g_led_timer_sem;
static Timer_t g_led_timer;

static void led_init(void)
{
  GPIO_Config(LED_PORT, LED1_PIN, GPIO_CNF_OUT_PP, GPIO_MODE_OUTPUT);
  GPIO_Config(LED_PORT, LED2_PIN, GPIO_CNF_OUT_PP, GPIO_MODE_OUTPUT);
  GPIO_Config(LED_PORT, LED3_PIN, GPIO_CNF_OUT_PP, GPIO_MODE_OUTPUT);
  LED1_OFF();
  LED2_OFF();
  LED3_OFF();
}

static void led_toggle(uint8_t pin)
{
  uint8_t cur = GPIO_Read_Pin(LED_PORT, pin);
  GPIO_Write_Pin(LED_PORT, pin, (cur != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void delay_ms_bm(uint32_t ms)
{
  volatile uint32_t n = ms * 100000U;
  while (n-- != 0U) {
    __asm volatile("nop");
  }
}

static void app_panic(const char *step, int32_t err)
{
  debug_print("FATAL: %s failed (%d)\r\n", step, (int)err);

  for (;;) {
    led_toggle(LED3_PIN);
    delay_ms_bm(120U);
  }
}

static void i2c_scan(I2C_t i2c)
{
  uint8_t count = 0U;

  debug_print("I2C scan (0x08-0x77):\r\n");

  for (uint8_t addr = 0x08U; addr <= 0x77U; addr++) {
    I2C_Start(i2c);
    uint8_t ack = I2C_Transmit_Address(i2c, addr, I2C_WRITE);
    I2C_Stop(i2c);

    if (ack != 0U) {
      debug_print("  [0x%x] ACK%s\r\n", (unsigned)addr,
                  (addr == AHT20_I2C_ADDR) ? " <- AHT20" : "");
      count++;
    }
  }

  if (count == 0U) {
    debug_print("  No devices found.\r\n");
    debug_print("  Check pull-ups, power, and SCL/SDA routing.\r\n");
  }

  debug_print("\r\n");
}

static void timer_signal_led_task(void *arg)
{
  (void)arg;
  (void)OS_SemPost(&g_led_timer_sem);
}

static void task_led_blink(void *arg)
{
  (void)arg;

  for (;;) {
    led_toggle(LED1_PIN);
    OS_Task_Delay(250U);
  }
}

static void task_sensor_logger(void *arg)
{
  uint32_t sample = 0U;
  (void)arg;

  for (;;) {
    AHT20_Data_t data;
    AHT20_Status_t st = AHT20_Read(I2C1, &data);

    if (st == AHT20_OK) {
      int32_t t10 = (int32_t)(data.temperature_c * 10.0f);
      int32_t rh10 = (int32_t)(data.humidity_pct * 10.0f);

      debug_print("[sensor:%u] T=%d.%u C  RH=%d.%u%%\r\n",
                  (unsigned)sample,
                  (int)(t10 / 10),
                  (unsigned)((uint32_t)t10 % 10U),
                  (int)(rh10 / 10),
                  (unsigned)((uint32_t)rh10 % 10U));
      LED3_ON();
    } else {
      debug_print("[sensor:%u] AHT20 err=%u (1=NACK 2=BUSY 3=TIMEOUT)\r\n",
                  (unsigned)sample,
                  (unsigned)st);
      led_toggle(LED3_PIN);
    }

    sample++;
    OS_Task_Delay(2000U);
  }
}

static void task_timer_led(void *arg)
{
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

int main(void)
{
  int32_t status;
  uint8_t tdre_ok;

  led_init();
  debug_print_init();

  tdre_ok = (SCI_SSR(OS_DEBUG_UART_CHANNEL) & SSR_TDRE) ? 1U : 0U;
  if (tdre_ok == 0U) {
    for (uint8_t i = 0U; i < 10U; i++) {
      led_toggle(LED3_PIN);
      delay_ms_bm(80U);
    }
  }

  debug_print("\r\n=== RA6M5 RTOS Application Test ===\r\n");
  debug_print("UART  : SCI7 P613/P614 @ %u baud\r\n",
              (unsigned)OS_DEBUG_UART_BAUDRATE);
  debug_print("I2C   : RIIC1 P512(SCL)/P511(SDA) @ 100 kHz\r\n");
  debug_print("TDRE  : %s\r\n", tdre_ok != 0U ? "OK" : "FAIL (check MSTPCRB)");

  I2C_Init(I2C1, 50U, I2C_SPEED_STANDARD);
  i2c_scan(I2C1);
  delay_ms_bm(120U);
  AHT20_Init(I2C1);

  OS_Init();

  status = OS_SemCreate(&g_led_timer_sem, 0U, 1U);
  if (status != OS_OK) {
    app_panic("OS_SemCreate", status);
  }

  status = OS_TimerCreate(&g_led_timer,
                          timer_signal_led_task,
                          (void *)0,
                          500U,
                          OS_TIMER_AUTO_RELOAD);
  if (status != OS_OK) {
    app_panic("OS_TimerCreate", status);
  }

  status = OS_Task_Create(&g_led_blink_tcb,
                          task_led_blink,
                          (void *)0,
                          TASK_PRIO_LED_BLINK,
                          "led_blink");
  if (status != OS_OK) {
    app_panic("OS_Task_Create led_blink", status);
  }

  status = OS_Task_Create(&g_sensor_tcb,
                          task_sensor_logger,
                          (void *)0,
                          TASK_PRIO_SENSOR,
                          "sensor");
  if (status != OS_OK) {
    app_panic("OS_Task_Create sensor", status);
  }

  status = OS_Task_Create(&g_timer_led_tcb,
                          task_timer_led,
                          (void *)0,
                          TASK_PRIO_LED_TIMER,
                          "led_timer");
  if (status != OS_OK) {
    app_panic("OS_Task_Create led_timer", status);
  }

  status = OS_TimerStart(&g_led_timer);
  if (status != OS_OK) {
    app_panic("OS_TimerStart", status);
  }

  debug_print("Tasks ready: LED1 blink, AHT20 logger, LED2 timer blink\r\n");
  LED3_ON();

  OS_Start();

  for (;;) {
    __asm volatile("nop");
  }
}
