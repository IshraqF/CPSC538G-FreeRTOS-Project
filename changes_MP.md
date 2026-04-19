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

TODO
