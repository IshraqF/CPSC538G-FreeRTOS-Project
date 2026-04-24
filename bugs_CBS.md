# Bugs for CBS

## Job Submission Delayed by EDF Preemption

`xCBSSubmitJob()` is called from a fixed-priority task. Since the EDF scheduler unconditionally preempts all fixed-priority tasks, the submitter does not get CPU time while any EDF tasks are ready. The submission call is deferred until the EDF ready list drains, so the arrival time `rj` seen by Rule 1/2 is later than the true job arrival. This inflates `rj`, which can cause Rule 1 to fire when Rule 2 should have applied, unnecessarily resetting the server's deadline to `rj + Ts` and refilling the budget to `Qs`, instead of reusing the residual budget from the current period.

**Workaround**: make the submitter task an EDF task with its own period and deadline so it is scheduled alongside other EDF work and not starved by it.
