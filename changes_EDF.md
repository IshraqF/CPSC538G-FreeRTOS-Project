# Changes for EDF

---

## `src/FreeRTOSConfig.h`

### Added
- `configUSE_EDF_SCHEDULER`: macro, set as 1/0 to enable/disable EDF scheduler respectively
- `configEDF_MAX_TASKS`: macro, max capacity of accepted EDF tasks (default 100)
- `configEDF_MAX_SCHEDULING_POINTS`: macro, cap on scheduling points for demand-bound test (default 5000)
- `configEDF_ENABLE_DEBUG_LOG`: macro, use 1/0 to enable/disable ring buffer logging respectively

---

## `lib/FreeRTOS-Kernel/include/task.h`

### Added
- `xTaskCreateEDF()`: create an EDF periodic task with admission control, inputs: period, relative deadline, WCET, blocking time, and standard `xTaskCreate` params
- `vEDFDrainMissLog()`: drain deadline misses ring buffer to UART
- `vEDFDrainSwitchLog()`: drain context switcesh ring buffer to UART
- `vEDFPrintStats()`: print admitted EDF task parameters
- `vTaskDelayEDF()`: periodic delay, always updates job release time and absolute deadline

---

## `lib/FreeRTOS-Kernel/tasks.c`

### Modified
- `tskTCB`: added EDF fields: `xTaskPeriod`, `xTaskDeadline`, `xTaskComputationTime`, `xTaskIsEDF`, `xJobDeadline`, `xJobReleaseTime`
- `taskSELECT_HIGHEST_PRIORITY_TASK()`: picks earliest deadline in EDF ready list, if empty then fallback to fixed-priority. Also log context switches into ring buffer if debug logging enabled
- `xTaskIncrementTick()`: when unblocking a delayed task, use `prvEDFAddToReadyList()` instead of `prvAddTaskToReadyList()` for EDF tasks, `prvEDFUpdateJobOnUnblock()` to refresh absolute deadline, `prvEDFCheckDeadlineMiss()` on every tick, and skips priority bitmap reset for EDF tasks
- `xTaskResumeAll()`: same unblock handling for tasks that turned ready when scheduler was suspended
- `vTaskDelete()`: removes deleted EDF task from `xEDFAdmittedTasks[]`
- `prvInitialiseTaskLists()`: initializes `xEDFReadyTasksList`

### Added
- `xEDFReadyTasksList`: sorted list of ready EDF tasks (based on absolute deadline)
- `EDFTaskParams_t`: struct storing admitted task params (period, deadline, WCET, blocking time, etc.)
- `xEDFAdmittedTasks[]`: stores`EDFTaskParams_t`s
- `uxEDFAdmittedTaskCount`: number of admitted EDF tasks
- `EDFMissEntry_t`: struct for deadline miss log entries (task name, tick, deadline)
- `xEDFMissLog[]`: ring buffer for 16 miss entries
- `uxEDFMissLogHead / uxEDFMissLogTail`: head/tail indices for miss ring buffer
- `EDFSwitchEntry_t`: struct for context switch log entries (task name, tick, deadline)
- `xEDFSwitchLog[]`: ring buffer for 32 switch entries
- `uxEDFSwitchLogHead / uxEDFSwitchLogTail`: head/tail indices for switch ring buffer
- `xTaskCreateEDF()`: runs admission control, `xTaskCreate`, sets TCB fields, moves task from fixed-priority to EDF ready list, and stores params
- `prvEDFAddToReadyList()`: sets item value to `xJobDeadline` then inserts into builtin sorted EDF ready list
- `prvEDFUpdateJobOnUnblock()`: refreshes `xJobReleaseTime` and `xJobDeadline`
- `prvEDFCheckDeadlineMiss()`: checks if current EDF task missed its deadline, if so log miss
- `prvIsImplicitDeadlineSet()`: checks if all admitted tasks have implicit/constrained deadline, useful for LL vs demand bound
- `prvEDFAdmissionControlLL()`: LL utilization bound test
- `prvEDFAdmissionControlDemand()`: processor demand criterion test for constrained-deadline task sets
- `prvEDFAdmissionControl()`: wrapper that picks LL or demand bound based + additional sanity checks
- `vEDFPrintStats()`: prints admitted tasks info
- `vEDFDrainMissLog()`: drains miss ring buffer to UART
- `vEDFDrainSwitchLog()`: drains switch ring buffer to UART
- `vTaskDelayEDF()`: EDF-aware delay, always refreshes deadline, then re-sorts in EDF list
- `uxEDFGetAdmittedCount()`: returns number of admitted EDF tasks

---
