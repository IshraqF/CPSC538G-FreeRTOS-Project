# Design for EDF

## Overview

This implementation adds EDF as a toggle-able layer on top of FreeRTOS's existing fixed-priority scheduler, toggled by `configUSE_EDF_SCHEDULER`

Key properties:
- **Dynamic priority**: each job's priority is its absolute deadline — sooner deadlines run earlier
- total utilization up to 1.0 is schedulable
- **Compatible with fixed-priority tasks**: EDF tasks run before fixed-priority tasks when both are ready & fixed-priority will fill idle slack

## New TCB Fields

Each TCB gets these fields when `configUSE_EDF_SCHEDULER == 1` (all in units of ticks):

`xTaskPeriod`: period in ticks
`xTaskDeadline`: relative deadline D
`xTaskComputationTime`: declared WCET
`xTaskIsEDF`: TRUE if task is EDF task
`xJobDeadline`: absolute deadline of current job (release + D)
`xJobReleaseTime`: when current job was released

These are set once during `xTaskCreateEDF()` and updated every period by `vTaskDelayEDF()`.

## EDF Ready List

`xEDFReadyTasksList` has type FreeRTOS `List_t` & holds all ready EDF tasks in ascending deadline order. FreeRTOS's sorted list insertion `vListInsert()` is reused with list item value set to `xJobDeadline`, so the head always has the earliest deadline.

This is different from the fixed-priority `pxReadyTasksLists[]`, when tasks created via `xTaskCreateEDF()`, it's moved out of fixed-priority lists into EDFs list.

## Task Selection

`taskSELECT_HIGHEST_PRIORITY_TASK()` macro is modified to use the EDF ready list first as the following pseudo-code:

```
if EDF list not empty:
  select head
  log context switch
else:
  original behaviour: fall back to highest fixed-priority ready queue
```

## vTaskDelayEDF()

FreeRTOS `vTaskDelayUntil()` doesn't update EDF metadata when a task finishes exactly on its period boundary, since no actual blocking occurs,`prvEDFUpdateJobOnUnblock` never fires and the deadline becomes stale. `vTaskDelayEDF()` called by EDF tasks at the end of its job (instead of `vTaskDelayUntil`) to *always* refresh the deadline fields:

```
nextRelease = previousWakeTime + period
jobDeadline = nextRelease + relativeDeadline

if currentTick < nextRelease:
  block until nextRelease (normal case)
else:
  re-sort task in EDF list with new deadline (no block)
```

## Admission Control

`xTaskCreateEDF()` runs admission control to verify the task set is still schedulable after adding a task with the following 2 algorithms:

### LL Bound

For implicit deadlines, Liu & Layland utilization bound: `sum(C_i / T_i) <= 1.0`. Also checks per-task feasibility: `C_i + B_i <= T_i`, where B_i is worst-case blocking time provided by user as `xBlockingTime` parameter to `xTaskCreateEDF()`. For EDF only tests, B_i=0.

### Processor Demand Criterion

For constrained deadlines, use Processor Demand Criterion since LL bound doesn't apply:

```
h(t) = sum(floor((t + T_i - D_i) / T_i) * C_i) for all tasks i
```

Checked at scheduling points `t = n * T_i + D_i` up to `2 * max(D_i)`. The test passes if `h(t) <= t` at every scheduling point. Scheduling points are sorted via insertion sort, duplicates skipped.

### Wrapper

`prvEDFAdmissionControl()` picks the appropriate test automatically: if all tasks have D == T, use LL bound, otherwise use demand bound. Basic sanity checks reject tasks with stuff like 0 period, 0 WCET, 0 deadline, or D > T.

### Admitted Tasks List

`xEDFAdmittedTasks[]` (size: `configEDF_MAX_TASKS` default 100) stores the parameters of all admitted tasks. When a new task is created, its params are saved in the first free slot. When a task is deleted, its slot is marked invalid as `uxEDFAdmittedTaskCount` decrements. Admission control iterates over this array.

## Unblock Handling

When an EDF task's delay expires and it wakes up, `prvEDFUpdateJobOnUnblock()` refreshes absolute deadline based on the current tick before inserting into the EDF ready list.

FreeRTOS normally uses a priority bitmap (`uxTopReadyPriority`) to quickly find the highest fixed-priority ready task. EDF tasks don't touch this bitmap since they have their own sorted list.

3 places in the kernel handle EDF tasks waking up:
- `xTaskIncrementTick()`: every tick, check if any delayed tasks are ready. If it's EDF task, insert into the EDF ready list, otherwise fixed-priority list
- `xTaskResumeAll()`: same thing, but for tasks that became ready while the scheduler was temporarily suspended
- `vTaskDelete()`: when EDF task is deleted, remove it from `xEDFAdmittedTasks[]` so future admission control checks don't count it

## Deadline Miss Detection

`prvEDFCheckDeadlineMiss()` runs every tick. For currently running task, if `currentTick > xJobDeadline`, the miss is logged into debug ring buffer. The task isn't killed, instead continues running and the miss is just reported later.

## Debug Logging

2 ring buffers used for debugging:

1. `xEDFMissLog[]`, 16 entries: saves task name, tick, and deadline for every deadline miss
2. `xEDFSwitchLog[]`, 32 entries: saves task name, tick, and deadline for every EDF context switch

Drained to UART using `vEDFDrainMissLog()`/`vEDFDrainSwitchLog()`, called from a dedicated `vUARTDrainTask`. Ring buffers avoid printf inside the scheduler critical path. All logging is guarded by `configEDF_ENABLE_DEBUG_LOG`, easily disable-able.

## Backward Compatibility

All EDF code guarded by `#if (configUSE_EDF_SCHEDULER == 1)` for backwards compatibility
