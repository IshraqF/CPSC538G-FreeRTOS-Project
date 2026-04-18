# Changes for SRP

---

## `src/FreeRTOSConfig.h`

### Modified
- `configEDF_MAX_TASKS`: macro changed from 16 to 100
- `configEDF_MAX_SCHEDULING_POINTS`: macro changed from 1000 to 5000
- `configTOTAL_HEAP_SIZE`: macro changed from 128KB to 200KB

### Added
- `configUSE_SRP`: macro; switch 1/0 to enable/disable SRP
- `configSRP_MAX_CEILING_DEPTH`: macro; max nesting depth for system ceiling stack

---

## `lib/FreeRTOS-Kernel/include/task.h`

### Modified
- `xTaskCreateEDF()`: added `xBlockingTime` field for SRP worst-case blocking time

### Added
- `vSRPPushCeiling()`: push resource ceiling onto system ceiling stack (called on semaphore take)
- `vSRPPopCeiling()`: pop a resource ceiling from system ceiling stack (semaphore give)
- `xSRPGetCurrentCeiling()`: returns system ceiling value
- `uxSRPGetCeilingStackDepth()`: returns ceiling stack depth
- `vSRPDrainEventLog()`: drain SRP event ring buffer -> UART
- `xTaskCreateEDFSharedGroup()`: create a shared stack task group for SRP stack sharing
- `uxEDFGetAdmittedCount()`: return count of admitted EDF tasks

---

## `lib/FreeRTOS-Kernel/include/queue.h`

### Added
- `vQueueSetResourceCeiling()`: set SRP resource ceiling on a queue/semaphore handle

---

## `lib/FreeRTOS-Kernel/include/semphr.h`

### Added
- `vSemaphoreSetResourceCeiling(xSemaphore, xCeiling)`: macro for wrapping `vQueueSetResourceCeiling()`

---

## `lib/FreeRTOS-Kernel/queue.c`

### Modified
- `Queue_t` (struct QueueDefinition): added `xResourceCeiling` field (default 0 means no SRP ceiling)
- `prvInitialiseNewQueue()`: initialize `xResourceCeiling` to 0
- `xQueueSemaphoreTake()`: after successful semaphore take, call `vSRPPushCeiling()` if the resource has a nonzero ceiling
- `prvCopyDataToQueue()`: on semaphore give, call `vSRPPopCeiling()` if the resource has a nonzero ceiling; set `xReturn` to `TRUE` to force a yield so scheduler can reevaluate after ceiling changes

### New
- `vQueueSetResourceCeiling()`: sets `xResourceCeiling` on a queue handle

---

## `lib/FreeRTOS-Kernel/tasks.c`

### Modified
- `tskTCB`: added `xPreemptionLevel` field, shorter deadline = higher preemption level
- `EDFTaskParams_t`: added `xMaxBlockingTime` field for SRP worst case blocking time
- `taskSELECT_HIGHEST_PRIORITY_TASK()`: previously picked the head item of `xEDFReadyTasksList`, now iterates the EDF ready list and picks first task passing SRP preemption test:
  1. `configUSE_SRP != 1` OR
  2. `pxTCB == pxCurrentTCB` (currently running task always passes), OR
  3. `pxTCB->xPreemptionLevel < xSRPCurrentCeiling` (task's preemption level exceeds system ceiling), OR
  4. `prvSRPTaskOwnsCeiling(pxTCB)` (task holds a resource that contributed to the current ceiling)
If no EDF task passes, default to fixed priority
- `xTaskCreateEDF()`: now takes extra `xBlockingTime` parameter, so scheduler knows worst-case blocking time for shared resource. Also sets each task's preemption level to its relative deadline
- `prvEDFAdmissionControlLL()`: Liu & Layland utilization test, modified to also check each task's computation + blocking time fits within deadline
- `prvEDFAdmissionControlDemand()`: processor demand test for constrained-deadline, modified to account for blocking time
- `prvEDFAdmissionControl()`: wrapper that passes blocking time to whichever test (LL/demand) is used

### New
- `SRPCeilingEntry_t`: each entry on ceiling stack stores a ceiling value and which task put it there
- `xSRPCeilingStack[]`: array holding the actual ceiling stack, resource ceilings get push/popped here
- `uxSRPCeilingStackTop`: tracks how many entries are on the ceiling stack
- `xSRPCurrentCeiling`: current system ceiling (smallest/ most restrictive deadline among held resources), if no resource held, equals `portMAX_DELAY`
- `SRPEventEntry_t`: struct for logging lock/unlock events (stores task name, tick, ceiling value, and whether it was a lock or unlock)
- `xSRPEventLog[]`: ring buffer stores the last 16 SRP events
- `uxSRPEventLogHead / uxSRPEventLogTail` head/tail indices for ring buffer
- `EDFSharedGroupContext_t`: context for shared-stack dispatcher, stores job function pointer and number of jobs sharing the stack
- `prvSRPTaskOwnsCeiling()`: checks if task is the one that raised system ceiling.
- `vSRPPushCeiling()`: when task locks a resource, pushes resource's ceiling onto the stack
- `vSRPPopCeiling()`: when task unlocks a resource, removes the top entry and recalculate new system ceiling
- `xSRPGetCurrentCeiling()`: returns the current system ceiling
- `uxSRPGetCeilingStackDepth()`: returns how many entries are on ceiling stack
- `vSRPDrainEventLog()`: prints all buffered SRP lock/unlock events
- `prvEDFSharedGroupDispatcher()`: single task that runs multiple jobs sequentially using the same stack
- `xTaskCreateEDFSharedGroup()`: public API to create a group of jobs that share 1 stack, number of jobs and shared function are specified
- `uxEDFGetAdmittedCount()`: returns how many admitted EDF tasks

---

## `src/main.c`

### Modified
- `vUARTDrainTask()`: updated to flush SRP event logs, priority lowered from 3 -> 1 to not interfere with EDF scheduling
- Existing `xTaskCreateEDF()` calls updated to pass blocking time (use 0 for tests that don't use SRP)

### New
New tests explained in testing_SRP.md
