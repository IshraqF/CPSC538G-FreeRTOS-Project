# Design for CBS

## CBS Overview

The Constant Bandwidth Server (CBS) is a scheduling algorithm that allows tasks with soft deadlines to be scheduled alongside hard real-time tasks. It treats each soft real-time task as a "server" with a specified budget and period. The server can execute for at most its budget within each period; if it exhausts its budget early, its deadline is postponed and its budget is refilled, pushing it to the back of the EDF queue. The CBS adds:

- **Max Budget (Qs)**: maximum execution time allowed for the server in each period
- **Period (Ts)**: the length of each server period
- **Current Budget (cs)**: remaining execution time in the current period

## CBS Rules

- When a CBS server receives a job and the server is idle,
    - If $c_s \geq (d_s - r_j)U_s$, then $d_s = r_j + T_s$ and $c_s = Q_s$ (Rule 1)
    - Else, deadline and current budget remain unchanged (Rule 2)
- When a job executes, the current budget is decremented each tick
- When the current budget is exhausted, $d_s = d_s + T_s$ and $c_s = Q_s$ (Rule 3)

## Server Creation and Initialization

`xTaskCreateCBS()` creates a CBS server as a FreeRTOS task with its own queue for receiving jobs. The server is initialized in an idle state, not consuming CPU until a job arrives. The TCB fields are populated as follows:

- The TCB fields are initialized:
    - `xTaskIsCBS = pdTRUE` to identify this task as a CBS server
    - `xCBSBudget = Qs`, `xCBSMaxBudget = Qs`, and `xCBSPeriod = Ts` from the parameters
    - `xCBSPending = 0` since no jobs are pending at creation
    - `xCBSExecuting = pdFALSE` since the server is idle at creation
    - `xJobDeadline` and `xJobReleaseTime` are initialized to 0

No admission control is run at creation time. CBS bounds the server's bandwidth to `Us = Qs / Ts` by construction, so it is up to the user to ensure `sum(Ui) + Us <= 1.0` across all tasks.

## Job Acceptance and Rule 1 / Rule 2

`xCBSSubmitJob()` is called by a submitter task or ISR to enqueue a job. Inside a critical section:

- `xCBSPending` is incremented.
- If the server was idle before this call (`xCBSPending` was 0), Rule 1/2 is evaluated using the current tick `rj`:

**Rule 1 condition** — the textbook form `cs >= (ds - rj) * Us` uses `Us = Qs / Ts`, which requires division. Multiplying both sides by `Ts` gives the integer-safe equivalent used in the code:

```
rj * Qs + cs * Ts >= ds * Qs
```

- If the condition holds **(Rule 1)**: `ds = rj + Ts` and `cs = Qs`. The deadline is reset and the budget is fully refilled.
- Otherwise **(Rule 2)**: `ds` and `cs` remain unchanged.

If the server was **not** idle (already has a pending or executing job), rules 1 and 2 are not applied. When the server is active and a job arrives in the server, it is stored in a FIFO queue . Once the running task finished executing, the next job is served with the same deadline and remaining budget as the previous job.

## Adding Jobs to the EDF Ready List

If the server is not yet in the EDF ready list (its state list item has no container), `prvCBSAddToReadyList()` inserts it.

`prvCBSAddToReadyList()` inserts the CBS server into `xEDFReadyTasksList`, the same sorted list used by all EDF tasks. It sets the task's list item value to `xJobDeadline` and does a manual linked-list walk to find the correct sorted position, using strict `<` comparison for stable ordering.

The `prvAddTaskToReadyList()` macro routes CBS tasks to `prvCBSAddToReadyList()` instead of the normal EDF path, so that whenever the kernel moves a CBS task to a ready state, it always lands in the correct sorted position with its CBS-assigned deadline.

## Job Execution and Budget Decrement

`prvCBSServerTask` is the internal task function for every CBS server:

```
for ever:
    block until a new job arrives
    increment the number of pending jobs
    run the job to completion
    decrement the number of pending jobs
```

The task blocks on its queue when idle, so it does not consume CPU and is not in the EDF ready list. When a job arrives via `xCBSSubmitJob`, the queue receive unblocks, `xCBSExecuting` is set, and the job function runs to completion.

Budget decrement happens in `xTaskIncrementTick()`, once per tick, guarded by two conditions:

- If the task is a CBS task
- If CBS is executing the task

Gating on `xCBSExecuting` is critical: the server task can be in the EDF ready list (scheduled) but waiting on `xQueueReceive` between jobs. Budget must not drain during that idle wait. Only real job work should consume bandwidth.

## Rule 3 — Budget Exhaustion

Still in `xTaskIncrementTick()`, immediately after the decrement, if `xCBSBudget == 0`:

- The task is removed from `xEDFReadyTasksList`
- deadline is postponed by one period
- budget is fully refilled
- re-inserts the server with the new (later) deadline
- forces a context switch at the end of the tick ISR

## Backward Compatibility

All CBS code is guarded by `#if (configUSE_CBS_SERVER == 1)`. When disabled:

- No CBS fields (`xTaskIsCBS`, `xCBSBudget`, `xCBSQueue`, etc.) exist in the TCB
- `prvAddTaskToReadyList` only routes to EDF or fixed-priority paths; the CBS branch is not compiled
- `prvEDFUpdateJobOnUnblock` runs normally for all EDF tasks (the CBS early-return is absent)
- No budget decrement or Rule 3 code appears in `xTaskIncrementTick`
- `xTaskCreateCBS` and `xCBSSubmitJob` are not compiled
