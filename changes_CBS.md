# Changes for CBS

---

## `src/FreeRTOSConfig.h`

### Added
- `configUSE_CBS_SERVER`: macro, set as 1/0 to enable/disable CBS server respectively
- `configCBS_QUEUE_LENGTH`: macro, max number of pending jobs per CBS server queue (default 16)

---

## `lib/FreeRTOS-Kernel/include/task.h`

### Added
- `CBSJob_t`: struct holding a job function pointer (`pxFunction`) and its argument (`pvParameters`)
- `xTaskCreateCBS()`: create a CBS server task with a given max budget `xQs` and period `xTs`
- `xCBSSubmitJob()`: submit a job to a CBS server from a task context; applies Rule 1/2 and inserts server into EDF ready list
- `xCBSSubmitJobFromISR()`: ISR-safe variant of `xCBSSubmitJob`

---

## `lib/FreeRTOS-Kernel/tasks.c`

### Modified
- `tskTCB`: added CBS fields: `xTaskIsCBS`, `xCBSBudget`, `xCBSMaxBudget`, `xCBSPeriod`, `xCBSPending`, `xCBSQueue`, `xCBSExecuting`
- `prvAddTaskToReadyList()` macro: added CBS branch — if task is a CBS server (`xTaskIsCBS == pdTRUE`), routes to `prvCBSAddToReadyList()` instead of `prvEDFAddToReadyList()`
- `prvEDFUpdateJobOnUnblock()`: added early return for CBS tasks — CBS manages its own deadlines so the standard EDF deadline refresh must not overwrite them
- `xTaskIncrementTick()`: added two CBS blocks — (1) decrement `xCBSBudget` by 1 each tick when `xTaskIsCBS == TRUE` and `xCBSExecuting == TRUE`; (2) apply Rule 3 when `xCBSBudget` reaches 0: remove from ready list, postpone deadline by `xCBSPeriod`, refill budget to `xCBSMaxBudget`, re-insert with new deadline, set `xSwitchRequired = pdTRUE`

### Added
- `prvCBSAddToReadyList()`: inserts a CBS server into `xEDFReadyTasksList` sorted by `xJobDeadline`, using a manual linked-list walk with strict `<` comparison for stable ordering
- `prvCBSServerTask()`: internal task function for every CBS server; loops on `xQueueReceive(portMAX_DELAY)`, sets `xCBSExecuting = pdTRUE`, runs the job function, clears `xCBSExecuting`, decrements `xCBSPending`
- `xTaskCreateCBS()`: creates the job queue, creates the server task via `prvCreateTask`, initializes all CBS TCB fields
- `xCBSSubmitJob()`: inside a critical section, increments `xCBSPending`, applies Rule 1/2 if server was idle, calls `prvCBSAddToReadyList()` if server is not yet in the ready list, then sends the job to `xCBSQueue` via `xQueueSend` and yields

---

## `src/main.c`

### Modified
- `vUARTDrainTask()` priority raised from 1 -> 3 for CBS tests so that debug logs are flushed promptly between CBS server periods

### Added
- Test 30: CBS Lifecycle Test — verifies basic job submission and execution
- Test 31: CBS Rule 3 Test — submits a job that runs longer than the budget; verifies Rule 3 fires and deadline is postponed
- Test 32: CBS Rule 2 Test — submits jobs while server has residual budget; verifies deadline and budget remain unchanged (Rule 2)
- Test 33: CBS + Hard EDF Task Test — runs a CBS server alongside a hard EDF task; verifies the hard task meets its deadline while the CBS server is throttled
- Test 34: CBS Random Arrival Stress Test — submits multiple variable-length jobs at irregular intervals; verifies server stays within its bandwidth
- Test 35: CBS Random Arrival Stress Test (variant) — similar stress test with different job mix and parameters
