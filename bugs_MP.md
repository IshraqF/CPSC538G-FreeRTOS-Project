# Bugs for MP

# Global EDF

## `xLWT` initialization value "bug"

Not exactly a bug, but a runtime quirk to know. `xLWT` (last wakeup time) tracks when a task last started its period. `vTaskDelayEDF` uses it to compute next wakeup time `xLWT + xPeriod`. After jobs complete, `vTaskDelayEDF` updates `xLWT` and sleeps until next period boundary.

The "correct" initial value of `xLWT` depends on when the task is created:

- Tasks created before the scheduler starts (tick 0): `xLWT = 0` is "correct." The first wake time is `0 + T`, which aligns the task's period to the start of the system.
- Tasks created at runtime (tick > 0): `xLWT = xTaskGetTickCount()` is "correct." A task created at tick 506 with T=200 should wake at 706, 906, etc. If `xLWT = 0`, it computes wake times of 200, 400, 600... all in the past, so the task never sleeps and cascades into deadline misses.

`vTaskDelayEDF` works correctly given a proper `xLWT`, it's just user's responsibility to initialize `xLWT`value based on when the task starts.

---

# Partitioned EDF

TODO
