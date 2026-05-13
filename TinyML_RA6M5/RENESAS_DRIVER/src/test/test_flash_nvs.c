#include "test_flash_nvs.h"

#include "debug_print.h"
#include "drv_flash_hp.h"
#include "kernel.h"

#include <stdint.h>
#include <stddef.h>

#define FLASH_NVS_TEST_TASK_PRIO       6U
#define FLASH_NVS_TEST_START_DELAY_MS  1200U

#define FLASH_NVS_TEST_RAW_LEN         21U
#define FLASH_NVS_TEST_WRITE_LEN       \
    ((((FLASH_NVS_TEST_RAW_LEN) + DATA_FLASH_WRITE_WORD_SIZE - 1UL) / DATA_FLASH_WRITE_WORD_SIZE) * \
     DATA_FLASH_WRITE_WORD_SIZE)

static OS_TCB_t s_flash_nvs_test_tcb;

static const uint8_t s_flash_nvs_pattern[FLASH_NVS_TEST_RAW_LEN] =
{
    0x52U, 0x41U, 0x36U, 0x4DU, 0x35U, 0x5FU, 0x4EU, 0x56U,
    0x53U, 0x5FU, 0x54U, 0x45U, 0x53U, 0x54U, 0x5FU, 0x30U,
    0x31U, 0xA5U, 0x5AU, 0xC3U, 0x3CU
};

static void flash_nvs_test_suspend_forever(void)
{
    for (;;)
    {
        OS_Task_Delay(OS_WAIT_FOREVER);
    }
}

static void task_flash_nvs_test(void *arg)
{
    flash_hp_status_t status = FLASH_HP_OK;
    const char * stage = "init";
    const char * fail_stage = "init";
    volatile uint8_t const * p_flash =
        (volatile uint8_t const *)(uintptr_t)DATA_FLASH_LAST_BLOCK_ADDR;
    uint8_t write_buf[FLASH_NVS_TEST_WRITE_LEN];
    uint8_t read_buf[FLASH_NVS_TEST_RAW_LEN];
    uint32_t index;
    uint32_t fail_index = 0UL;
    uint8_t expected_byte = 0U;
    uint8_t actual_byte = 0U;
    uint8_t compare_ok = 1U;
    uint8_t flash_active = 0U;
    uint32_t fail_fstatr = 0UL;
    uint16_t fail_fentryr = 0U;
    uint8_t fail_fastat = 0U;
    uint32_t fail_fsaddr = 0UL;
    uint16_t fail_fcmdr = 0U;

    (void)arg;

    OS_Task_Delay(FLASH_NVS_TEST_START_DELAY_MS);

    for (index = 0UL; index < FLASH_NVS_TEST_WRITE_LEN; index++)
    {
        write_buf[index] = 0xFFU;
    }

    for (index = 0UL; index < FLASH_NVS_TEST_RAW_LEN; index++)
    {
        write_buf[index] = s_flash_nvs_pattern[index];
        read_buf[index] = 0U;
    }

    debug_print("[FLASH NVS TEST] start addr=0x%x erase=%uB raw=%uB write=%uB\r\n",
                (unsigned int)DATA_FLASH_LAST_BLOCK_ADDR,
                (unsigned int)DATA_FLASH_BLOCK_SIZE,
                (unsigned int)FLASH_NVS_TEST_RAW_LEN,
                (unsigned int)FLASH_NVS_TEST_WRITE_LEN);

    status = flash_hp_init();
    if (status == FLASH_HP_OK)
    {
        flash_active = 1U;
        stage = "erase";
        status = flash_hp_erase(DATA_FLASH_LAST_BLOCK_ADDR, 1U);
    }

    if (status == FLASH_HP_OK)
    {
        stage = "write";
        status = flash_hp_write(DATA_FLASH_LAST_BLOCK_ADDR, write_buf, FLASH_NVS_TEST_WRITE_LEN);
    }

    if (status != FLASH_HP_OK)
    {
        fail_stage = stage;
        fail_fstatr = FSTATR;
        fail_fentryr = FENTRYR;
        fail_fastat = FASTAT;
        fail_fsaddr = FSADDR;
        fail_fcmdr = FCMDR;
    }

    stage = "exit";
    if (flash_active != 0U)
    {
        flash_hp_exit();
    }

    if (status == FLASH_HP_OK)
    {
        stage = "verify";
        for (index = 0UL; index < FLASH_NVS_TEST_RAW_LEN; index++)
        {
            read_buf[index] = p_flash[index];
            if (read_buf[index] != s_flash_nvs_pattern[index])
            {
                compare_ok = 0U;
                fail_index = index;
                expected_byte = s_flash_nvs_pattern[index];
                actual_byte = read_buf[index];
                break;
            }
        }
    }

    if (status != FLASH_HP_OK)
    {
        debug_print("[FLASH NVS TEST] FAIL status=%u stage=%s FSTATR=0x%x FENTRYR=0x%x FASTAT=0x%x FSADDR=0x%x FCMDR=0x%x\r\n",
                    (unsigned int)status,
                    fail_stage,
                    (unsigned int)fail_fstatr,
                    (unsigned int)fail_fentryr,
                    (unsigned int)fail_fastat,
                    (unsigned int)fail_fsaddr,
                    (unsigned int)fail_fcmdr);
    }
    else if (compare_ok != 0U)
    {
        debug_print("[FLASH NVS TEST] PASS addr=0x%x bytes=%u\r\n",
                    (unsigned int)DATA_FLASH_LAST_BLOCK_ADDR,
                    (unsigned int)FLASH_NVS_TEST_RAW_LEN);
    }
    else
    {
        debug_print("[FLASH NVS TEST] FAIL index=%u exp=0x%x got=0x%x\r\n",
                    (unsigned int)fail_index,
                    (unsigned int)expected_byte,
                    (unsigned int)actual_byte);
    }

    flash_nvs_test_suspend_forever();
}

void test_flash_nvs_init(void)
{
    int32_t status;

    status = OS_Task_Create(&s_flash_nvs_test_tcb,
                            task_flash_nvs_test,
                            (void *)0,
                            FLASH_NVS_TEST_TASK_PRIO,
                            "flash_nvs");
    if (status != OS_OK)
    {
        debug_print("[FLASH NVS TEST] task create failed (%d)\r\n", (int)status);
    }
}