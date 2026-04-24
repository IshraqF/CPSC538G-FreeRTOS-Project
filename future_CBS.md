# Future Improvements for CBS

## Job Submission Delay from Fixed-Priority Submitters

`xCBSSubmitJob()` is typically called from a fixed-priority task. Since the EDF scheduler unconditionally preempts all fixed-priority tasks, the submitter may not get CPU time to call `xCBSSubmitJob` while any EDF tasks are ready. This delays `rj` (the arrival time seen by Rule 1/2), which can cause Rule 1 to fire when Rule 2 should have applied, unnecessarily resetting the deadline and refilling the budget, and inflating the server's effective bandwidth use.

Two approaches to fix this:

- Make the submitter task an EDF task with its own period and deadline, so it is scheduled alongside other EDF work and not starved by it
- Submit jobs from a timer ISR using `xCBSSubmitJobFromISR` (declared in `task.h` but not yet implemented) so arrival is interrupt-driven and independent of task scheduling delays

## CBS Admission Control

`xTaskCreateCBS()` does not call `prvEDFAdmissionControl()` or register the server in `xEDFAdmittedTasks[]`. The CBS server's bandwidth `Us = Qs / Ts` is not accounted for in the scheduler's utilization check, so it is possible to over-admit the system. The admission control should include the CBS bandwidth and reject creation if `sum(Ui) + Us > 1.0`.
