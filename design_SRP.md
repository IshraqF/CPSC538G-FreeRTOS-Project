# Design for SRP

## SRP Overview

The Stack Resource Policy (SRP) prevents priority inversion and deadlocks when tasks share resources. Namely, it adds:

- **Preemption level**: `pi_i = 1/D_i`: shorter relative deadline is higher preemption level
- **Resource ceiling**: `C(R_k)`: equals highest preemption level among all tasks using the resource
- **System ceiling**: `Pi`: highest ceiling among all held resources
- SRP scheduling rule: A task `j` can only preempt if `pi_j > Pi`
- Guarantee: A task blocks at most once per job, and _only before_ it starts executing

## Preemption Level Representation

Since relative deadline inversely proportional to preemption level, and relative deadline directly stored as preemption level, the original SRP comparison `pi_j > Pi` becomes `D_j < system_ceiling_deadline`. This avoids storing inverse values. System ceiling `xSRPCurrentCeiling` is initialized to `portMAX_DELAY`, which acts as sentinel value to mean "no ceiling, any task can preempt".

## System Ceiling Stack

Array (`xSRPCeilingStack[]`) with stack pointer (`uxSRPCeilingStackTop`), and maximum depth is configurable via `configSRP_MAX_CEILING_DEPTH` (defaulted to 8)

Each entry stores:
- `xCeiling` — the resource ceiling value (a relative deadline)
- `pxOwner` — pointer to TCB that locked the resource

**Why store the owner?** After a preempting task finishes, the scheduler reselects via `taskSELECT_HIGHEST_PRIORITY_TASK()`. The previously running resource-holding task may have a preemption level equal to the system ceiling (because it raised it), so without the owner check, the strict `xPreemptionLevel < xSRPCurrentCeiling` comparison would block it from resuming.

**Why not `<=`?** `<=` lets unrelated tasks with the same preemption level bypass the ceiling, violating SRP.

```
Push(ceiling, owner):
  stack[top] = {ceiling, owner}
  top++
  if ceiling < currentCeiling:
    currentCeiling = ceiling

Pop():
  top--
  currentCeiling = MAX
  for each remaining entry:
    if entry.ceiling < currentCeiling:
      currentCeiling = entry.ceiling
```

## Scheduler Integration

The SRP check is integrated into the `taskSELECT_HIGHEST_PRIORITY_TASK()` macro, which iterates the ascending list of absolute deadline, and selects first task that passes the SRP test:

```
for each task in EDF ready list:
  if SRP_DISABLED
    OR task == currentlyRunningTask
    OR task.preemptionLevel < systemCeiling
    OR task owns a ceiling stack entry:

    select this task
    break
```

If no EDF tasks pass, default to fixed priority task selection

**Why iterate instead of just using the head?** In SRP, earliest deadline task may be blocked by system ceiling, so a later deadline task owning the ceiling-raising resource should run instead.

## Resource Ceiling Assignment

After creating semaphore, user sets resource ceilings via `vSemaphoreSetResourceCeiling(xSemaphore, xCeiling)`. The value is the minimum relative deadline among all tasks using the resource: `ceiling(R) = min{D_i : t_i uses R}`.

If ceiling = 0, it means "no SRP," so treat it as normal FreeRTOS semahpore, so existing code is unaffected by SRP additions (backwards compatibility).

Ceiling stored as `xResourceCeiling` in `Queue_t` struct. On `xSemaphoreTake`, if the ceiling is non-zero, call `vSRPPushCeiling()`, and on `xSemaphoreGive`, if the ceiling is non-zero, then call `vSRPPopCeiling()` and yield so the scheduler checks which task should run.

## Admission Control with Blocking Times

The `xTaskCreateEDF()` API accepts a `xBlockingTime` parameter (worst case blocking time B_i). This is specified by user on task creation so the API signature change smaller.

### LL-bound path (implicit-deadline tasks, D = T)

2 checks:
1. **Per-task feasibility**: `C_i + B_i <= T_i` for every task
2. **Utilization bound**: `sum(C_i / T_i) <= 1.0`

The utilization is computed with fixed-point arithmetic with a scale factor of 10000, replacing the exact arithmetic, which might overflow with 100 tasks.

### Demand-bound path (constrained-deadline tasks, D < T)

The processor demand function `h(t)` is extended to include blocking:

```
h(t) = sum(floor((t + T_i - D_i) / T_i) * C_i) + max{B_i : D_i <= t}
```

The max blocking time among tasks with deadlines <= t is summed to the demand at each scheduling point, then test checks `h(t) <= t` for all scheduling points.

## Stack Sharing

SRP guarantees that tasks with the same preemption level/relative deadline never occupy stack space simultaneously. Once task starts, it should complete without blocking, achieved via `xTaskCreateEDFSharedGroup()`:

- 1 FreeRTOS task is created with a single stack
- Dispatcher function runs `uxJobCount` job functions sequentially within each period
- All jobs share same stack since they execute sequentially

This reduces memory from `O(N * stack_per_task)` to `O(1 * stack_per_group)`, numbers explored with 100 tasks test in Tests 11/12.

## Event Logging

SRP lock/unlock events are logged into ring buffer `xSRPEventLog[]` (capped at 16 entries) from `vSRPPushCeiling()` and `vSRPPopCeiling()`. These functions execute inside semaphore take/give paths. The ring buffer is flushed to UART by `vSRPDrainEventLog()`, called from `vUARTDrainTask` at priority 1 during idle. `printf()` isn't used as it's not thread safe.

Every event stores: task name, tick count, ceiling value, and event type (L=lock, U=unlock)

## Backward Compatibility

All SRP code guarded by `#if (configUSE_SRP == 1)`. If disabled, then:
- No `xPreemptionLevel` field in the TCB
- No ceiling stack or event log
- Scheduler macro picks the head of the EDF ready list directly
- Semaphores have no `xResourceCeiling` overhead
- `xBlockingTime` parameter in `xTaskCreateEDF()` equals 0

## Overrun / Deadline Miss Handling

When deadline miss detected by `prvEDFCheckDeadlineMiss()`, the event is logged into the ring buffer. The task continues executing and miss is reported instead of killing. SRP's bounded blocking guarantee means that if tasks are correctly admitted (C_i + B_i <= D_i), deadline misses shouldn't occur. If it does, then it indicates incorrect WCET/blocking estimates.
