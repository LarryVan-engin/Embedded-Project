#include "test_rtos.h"
#include "kernel.h"
#include "debug_print.h"

static OS_TCB_t g_test_low_tcb;
static OS_TCB_t g_test_high_tcb;

static volatile int g_preempt_step = 0;

static void test_high_prio_task(void *arg)
{
    (void)arg;
    
    /* 
     * If preemption works correctly, this task must execute IMMEDIATELY after being 
     * created from low_prio_task, before low_prio_task executes the next instruction.
     */
    if (g_preempt_step == 1) {
        g_preempt_step = 2;
    }
    
    /* Suspend task indefinitely to yield CPU to other tasks */
    for (;;) {
        OS_Task_Delay(0xFFFFFFFFU); 
    }
}

static void test_low_prio_task(void *arg)
{
    (void)arg;
    int32_t status;
    
    debug_print("\r\n[RTOS TEST] Starting Preemptive Test...\r\n");
    
    g_preempt_step = 1;
    
    /* 
     * Create a higher priority task. Here low_prio has priority 5, we create high_prio with priority 4.
     * (0 is highest priority, 31 is lowest).
     */
    status = OS_Task_Create(&g_test_high_tcb,
                            test_high_prio_task,
                            (void *)0,
                            4U,
                            "test_high");
                            
    if (status != OS_OK) {
        debug_print("[RTOS TEST] OS_Task_Create failed!\r\n");
        for (;;) { OS_Task_Delay(0xFFFFFFFFU); }
    }
    
    /* 
     * Immediately after OS_Task_Create, the CPU should be preempted by test_high_prio_task.
     * Therefore, when code reaches this point, test_high_prio_task HAS already executed and set step = 2.
     */
    if (g_preempt_step == 2) {
        debug_print("[RTOS TEST] Preemptive Scheduling: PASS\r\n");
    } else {
        debug_print("[RTOS TEST] Preemptive Scheduling: FAIL (step = %d)\r\n", g_preempt_step);
    }
    
    /* End test, suspend this task indefinitely so the main system continues running normally */
    for (;;) {
        OS_Task_Delay(0xFFFFFFFFU);
    }
}

void test_rtos_preemptive_init(void)
{
    /* Create Low Prio Task. Priority 5 is low enough to not compete with main system. */
    OS_Task_Create(&g_test_low_tcb,
                   test_low_prio_task,
                   (void *)0,
                   5U,
                   "test_low");
}
