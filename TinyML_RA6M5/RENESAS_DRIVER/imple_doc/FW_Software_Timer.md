# FW_Software_Timer

Tags: #done #firmware #rtos #timer

Software Timer module with Timer Daemon Task architecture. Files: `Middleware/Kernel/software_timer.h`, `Middleware/Kernel/src/software_timer.c`. Kernel core: [[FW_Scheduler_Core]]. Semaphore (internal): [[FW_Semaphore]].

---

## Architecture

```
SysTick_Handler (ISR, every 1ms)
    │
    ▼ OS_TimerTick()
    Decrement running timers
    Mark expired → OS_TIMER_EXPIRED
    Post os_timer_sem (binary semaphore)
    │
    ▼ Timer Daemon Task (priority 1)
    Pend on os_timer_sem
    Process all expired timers
    Call callbacks in TASK context
    Auto-reload: reset remaining
    One-shot: stop timer
```

**Key design**: callbacks execute in the Timer Daemon Task, NOT in ISR context. This keeps SysTick execution time minimal and allows callbacks to use OS APIs (delay, semaphore, etc.).

---

## Timer Structure

```c
typedef struct {
    TimerCallback_t  callback;    /* Function called on expiry      */
    void            *arg;         /* Argument passed to callback    */
    uint32_t         period;      /* Period in ticks                */
    volatile uint32_t remaining;  /* Ticks until next expiry        */
    TimerMode_t      mode;        /* ONE_SHOT or AUTO_RELOAD        */
    volatile TimerState_t state;  /* STOPPED / RUNNING / EXPIRED    */
} Timer_t;
```

---

## API

| Function | Description |
|----------|-------------|
| `OS_TimerCreate(timer, cb, arg, period, mode)` | Register a timer (does not start) |
| `OS_TimerStart(timer)` | Start or restart (remaining = period) |
| `OS_TimerStop(timer)` | Stop a running timer |
| `OS_TimerReset(timer)` | Reset countdown to full period |

`OS_TimerInit()` is called internally by `OS_Init()`.

---

## Timer Daemon Task

- Created by `OS_TimerInit()` during `OS_Init()`.
- Runs at `OS_TIMER_TASK_PRIORITY` (default: 1, high priority).
- Blocks on internal binary semaphore `os_timer_sem`.
- When signalled: iterates all timers, calls expired callbacks.
- Releases critical section before each callback (callbacks may call OS APIs).

---

## Configuration (rtos_config.h)

| Parameter | Default | Description |
|-----------|---------|-------------|
| `OS_MAX_SOFTWARE_TIMERS` | 16 | Max registered timers |
| `OS_TIMER_TASK_PRIORITY` | 1 | Daemon task priority |
| `OS_TIMER_TASK_STACK_SIZE` | 4096 | Daemon stack (bytes) |

---

## Demo: Timer → Semaphore → Task

In `main.c`, the interaction chain:

1. `timer_led2` fires every 1000 ms (auto-reload).
2. Callback calls `OS_SemPost(&sem_led2)`.
3. Task 2 (`task_led2_sem`) wakes from `OS_SemPend`.
4. Task 2 toggles LED2 (P007).

This demonstrates decoupled, event-driven task synchronisation.

---

## Related Notes

- [[FW_Scheduler_Core]] — Kernel core, SysTick handler calls OS_TimerTick
- [[FW_Semaphore]] — Timer daemon uses binary semaphore internally
- [[FW_Port_RA6M5]] — SysTick configuration, NVIC priorities
