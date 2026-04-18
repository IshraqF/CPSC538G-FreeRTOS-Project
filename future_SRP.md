# Future Improvements for SRP

## Automatic Resource Ceiling Computation

Currently, user must manually call `vSemaphoreSetResourceCeiling(sem, ceiling)` with the correct ceiling value (minimum relative deadline among all tasks using the resource). Instead, kernel could automatically compute ceillings from admitted tasks.

## Dynamic Ceiling Stack Depth

`configSRP_MAX_CEILING_DEPTH` is a fixed compile-time constant, exceeding this will fail configASSERTs. 8 was chosen for this value, but depending on the practical application, it may need to be adjusted. Instead, the stack size could change dynamically to account for varying workloads.

## Blocking Time Auto-Computation

Currently, user provides worst-case blocking time `xBlockingTime` when calling `xTaskCreateEDF()`. This requires user knowing which resources each task uses and the longest critical section among lower preemption-level tasks. Instead, kernel could compute this automatically if tasks declared their resource usage on creation.

## O(1) Ceiling Recomputation on Pop

`vSRPPopCeiling()` causes a scan of the remaining ceiling stack to find the new minimum. This is O(n) where n is the stack depth. Since the default is 8, it's small but future optimization could introduce a struct/mine-tracker to make this operation O(1).
