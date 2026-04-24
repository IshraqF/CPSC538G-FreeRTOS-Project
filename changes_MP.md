# Changes for MP

# Global EDF

## `src/FreeRTOSConfig.h`

### Modified

- `configNUMBER_OF_CORES`: changed 1 -> 2
- `configUSE_CORE_AFFINITY`: changed 0 -> 1 (enables FreeRTOS-SMP's task core affinity bitmask, used to pin/unpin tasks)

### Added

- `configGLOBAL_EDF_ENABLE`: macro, use 1/0 to enable/disable global EDF, if disabled, use partitioned EDF
- `configPARTITIONED_EDF_ENABLE`: set macro as `!(configGLOBAL_EDF_ENABLE)` so only one is guaranteed active
- `configUSE_PASSIVE_IDLE_HOOK`: set to 0, required by FreeRTOS-SMP when `configNUMBER_OF_CORES > 1`

---

## `lib/FreeRTOS-Kernel/include/task.h`

### Added

- `vTaskEDFSetCore()`: pins EDF task to a specified core, wraps `vTaskCoreAffinitySet`
- `vTaskEDFClearCore()`: allows EDF task to run on any core (sets `tskNO_AFFINITY`)

---

## `lib/FreeRTOS-Kernel/tasks.c`

### Modified

- `EDFSwitchEntry_t`: added `xCoreID` field, so switch log shows which core a task was selected on
- `prvSelectHighestPriorityTask(xCoreID)`: added global EDF block before the fixed-priority loop, iterates `xEDFReadyTasksList` and picks first task that's either: already running on this core, or not running on any core and allowed by core affinity. Uses `goto edf_selected` to skip the fixed-priority path
- `prvYieldForTask()`: added EDF path at top, when EDF task unblocks, find the core running the latest-deadline EDF task and calls `prvYieldCore()` on it, then Return early so the fixed-priority yield path is skipped.
- `prvEDFCheckDeadlineMiss()`: changed to iterate through `pxCurrentTCBs[xCore]` since multiple cores now
- `prvEDFAdmissionControlLL()`: utilization bound changed from `U <= 1.0` -> `U <= configNUMBER_OF_CORES * 1.0`
- `xTaskCreateEDF()`: sets `uxCoreAffinityMask` to `tskNO_AFFINITY` on new tasks so can run on either core unless specified
- `vEDFDrainSwitchLog()`: prints `C0`/`C1` prefix per entry when `configNUMBER_OF_CORES > 1`

### Added

- `vTaskEDFSetCore()`: sets core affinity bitmask to a single core
- `vTaskEDFClearCore()`: resets core affinity to `tskNO_AFFINITY`

---

## `src/main.c`

### Added

- Tests 17-22 for global EDF

---

# Partitioned EDF

## `lib/FreeRTOS-Kernel/tasks.c`

### Modified

- `prvSelectHighestPriorityTask(xCoreID)`: added partitioned EDF block before the fixed-priority loop, iterates `xEDFReadyTasksList[coreID]` and picks first task that's either: already running on this core, or not running on any core and allowed by core affinity. Uses `goto edf_selected` to skip the fixed-priority path
- `prvYieldForTask()`: added partitioned EDF path at top, when EDF task unblocks, if it's assigned to this core, preempt the currently running task on this core by calling `prvYieldCore()`. No cross-core preemption since tasks are statically partitioned.

### Added

- `prvEDFAdmissionControlLLParitioned()`: added to check per-core utilization instead of the global utilization
- `prvIsImplicitDeadlineSetOnCore()`: helper function to check if implicit deadline is set for any task on the core, used in admission control to determine if new task can be admitted with implicit deadline
- `prvEDFAdmissionControlDemandParitioned()`: processor demand criterion test for constrained-deadline task sets
- `prvEDFAdmissionControlPartitioned()`: wrapper that picks LL or demand bound based + additional sanity checks for partitioned EDF

## `src/main.c`

### Added

- Tests 23-29 for partitioned EDF
