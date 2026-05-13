# FW_Semaphore

Tags: #done #firmware #rtos #sync

Counting and Binary Semaphore module. Files: `Middleware/Kernel/semaphore.h`, `Middleware/Kernel/src/semaphore.c`. Kernel core: [[FW_Scheduler_Core]]. Timer integration: [[FW_Software_Timer]].

---

## Structure

```c
typedef struct {
    volatile uint32_t  count;                         /* Current count         */
    uint32_t           max_count;                     /* Upper bound           */
    OS_TCB_t          *wait_list[OS_SEM_MAX_WAITERS]; /* Blocked task pointers */
    uint32_t           wait_count;                    /* # tasks waiting       */
} Semaphore_t;
```

Binary semaphore: `max_count = 1`. Counting semaphore: `max_count > 1`.

---

## API

| Function | Description |
|----------|-------------|
| `OS_SemCreate(sem, init, max)` | Initialise semaphore with initial count and max |
| `OS_SemPend(sem, timeout)` | Acquire — blocks if count == 0 |
| `OS_SemPost(sem)` | Release — wakes highest-priority waiter |

### Timeout Modes

| Value | Meaning |
|-------|---------|
| `OS_NO_WAIT` (0) | Return immediately if unavailable |
| `OS_WAIT_FOREVER` (0xFFFFFFFF) | Block indefinitely |
| N | Block for N milliseconds |

---

## Priority-Ordered Wake

When `OS_SemPost` is called and tasks are waiting, the **highest-priority** waiter (lowest priority number) is woken first. This prevents priority inversion in the wait list.

---

## Timeout Mechanism

Integrated with the kernel's SysTick handler:

1. Task calls `OS_SemPend(sem, timeout)` with count == 0.
2. Task is added to semaphore's `wait_list`.
3. `blocked_on` in TCB is set to the semaphore pointer.
4. `delay_ticks` is set to the timeout value.
5. Task is removed from the ready list → `OS_TASK_BLOCKED`.

In `SysTick_Handler`:
- Blocked tasks with `delay_ticks != OS_WAIT_FOREVER` are decremented.
- When `delay_ticks` reaches 0 and `blocked_on != NULL`:
  - `OS_Sem_TimeoutHandler` removes the task from the semaphore's wait list.
  - `block_result = OS_ERR_TIMEOUT`.
  - Task returns to ready list.

---

## ISR Safety

`OS_SemPost` is safe to call from ISR context (SysTick, etc.). It uses `OS_EnterCritical` / `OS_ExitCritical` for atomicity and triggers `OS_Schedule` (which sets PENDSVSET) rather than performing a direct context switch.

---

## Related Notes

- [[FW_Scheduler_Core]] — TCB structure, ready list, scheduling
- [[FW_Software_Timer]] — Timer daemon uses a binary semaphore internally
- [[FW_Context_Switch]] — PendSV triggered by OS_Schedule after sem post
