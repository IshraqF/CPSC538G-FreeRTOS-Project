# Design for MP

# Global EDF

## Overview

Use a single shared ready list `xEDFReadyTasksList` for all EDF tasks. Both cores use from this list, always picking the earliest deadline task. Also tasks can migrate freely between cores between jobs.

## Task Selection `prvSelectHighestPriorityTask`

When a core needs to pick a task, it iterates `xEDFReadyTasksList` (sorted by increasing deadline) then selects the first task that satisfies any of:

1. Already running on this core (keep it)
2. Not running on any core, and core affinity allows this core (swap it in)

Skip if the task is running on the other core, preventing 2 cores from grabbing the same task, which is safe because `prvSelectHighestPriorityTask` runs inside `vTaskSwitchContext`, where both the ISR lock and task lock are held, so both cores are sequential.

If no EDF task found, do the existing fixed-priority selection, which picks idle tasks.

```
for each task in xEDFReadyTasksList (sorted by deadline):
  if task == currentlyRunningOnThisCore:
    keep it, done
  if task.runState == NOT_RUNNING:
    if coreAffinity allows this core:
      swap in, done
    else:
      skip
  else:
    running on other core, skip

if nothing found:
  fall through to fixed-priority
```

## Cross-Core Preemption `prvYieldForTask`

When an EDF task unblocks from `vTaskDelayEDF`, semaphore give, etc., `prvYieldForTask` decides if it should preempt a running task. The EDF path:

1. If task already running, do nothing
2. For each core, check what's running:
  - non-EDF task: always preemptable (treat as infinite deadline)
  - EDF task with later deadline than the unblocked task: add as preemption candidate
3. Pick the core running the latest-deadline task and call `prvYieldCore()` on it

`prvYieldCore` writes to inter-core FIFO, which triggers an interrupt on target core, causing interrupt which causes context switch, where the target core re-evaluates EDF ready list and picks the earlier-deadline task

## Admission Control

For implicit-deadline tasks on m identical cores, the LL bound becomes `U <= m`. Our implementation:

```c
return ( ullUtilSum <= ( LL_SCALE * configNUMBER_OF_CORES ) ) ? pdPASS : pdFAIL;
```

So with 2 cores, tasks can admit if total utilization <= 2 and the task feasibility check is still `C_i + B_i <= D_i`

Single-core `configNUMBER_OF_CORES == 1`, stays the same as before `U <= 1.0`

## Core Affinity/Pinning

`xTaskCreateEDF()` default sets `uxCoreAffinityMask = tskNO_AFFINITY` (all bits set), so tasks can migrate freely. 2 APIs allow pinning:

- `vTaskEDFSetCore(xTask, xCoreID)`: restricts task to 1 core
- `vTaskEDFClearCore(xTask)`: allows task to run on any core

These wrap FreeRTOS-SMP's `vTaskCoreAffinitySet()`, and affinity is always checked during both task selection and yield decisions.

## Deadline Miss

`prvEDFCheckDeadlineMiss()` runs every tick from the tick ISR (only on core 0). On multicore, it loops over all cores and checks each running EDF task's `xJobDeadline` against the current tick. Misses are logged and printed to UART. The task continues running even after a miss (not killed or restarted). This behaviour design was intentionally chosen since it's simpler to implement. Supporting killing/restarting requires saving checkpoints and additional threads to kill the running job.

## Switch Log

`EDFSwitchEntry_t` stores task name, tick, deadline, and core ID. Both cores write to same ring buffer, which is safe since writes only happen inside `vTaskSwitchContext` which holds locks. `vEDFDrainSwitchLog()` prints entries with prefix `C0`/`C1` to easily see which core tasks ran to.

## Configuration

All global code is guarded by `#if configUSE_EDF_SCHEDULER == 1 && configGLOBAL_EDF_ENABLE == 1`. The user sets:

- `configGLOBAL_EDF_ENABLE 1` for global EDF (default as per spec)
- `configGLOBAL_EDF_ENABLE 0` for partitioned EDF, `configPARTITIONED_EDF_ENABLE` is defined as `!(configGLOBAL_EDF_ENABLE)`, so use `configGLOBAL_EDF_ENABLE` as the switch
If EDF is disabled entirely, FreeRTOS runs its default fixed-priority scheduler.

## Thread Safety

All shared state access is sequential:

- `xEDFReadyTasksList`: thread-safe since only accessed in protected regions (`vTaskSwitchContext` holds both locks, `vTaskDelayEDF` uses `vTaskSuspendAll`, `xTaskIncrementTick` holds ISR lock)
- `pxCurrentTCBs[]`: only written in `vTaskSwitchContext`
- Switch log buffer: only written in `vTaskSwitchContext`
- Deadline miss log: only written from core 0's tick ISR

---

# Partitioned EDF

TODO
