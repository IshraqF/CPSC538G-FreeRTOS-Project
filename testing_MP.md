# Test Cases for MP

Each test is selected at compile time via `#define TEST_CASE N` at the top of `main.c`.

# Global EDF

## Test 17 — 2 Tasks, 2 Cores

2 identical EDF tasks that should run simultaneously, 1 per core. Verifies both cores are active and scheduling EDF tasks.

**Params**:

- Task A: T=200, D=200, C=80
- Task B: T=200, D=200, C=80
- Total U = 0.80 across 2 cores

**Expected behavior**:

- A & B run on different cores simultaneously

**Result**: PASS. Logs confirms tasks assigned to separate cores.

---

## Test 18 — 3 Tasks, 2 Cores (Migration)

3 tasks on 2 cores, so at least 1 task must migrate between cores across jobs. This is the core global EDF behavior: tasks aren't pinned, the scheduler puts the two earliest-deadline tasks on the two cores.

**Params**:

- Task A: T=100, D=100, C=30 (U=0.30)
- Task B: T=150, D=150, C=40 (U=0.27)
- Task C: T=200, D=200, C=50 (U=0.25)
- Total U = 0.82

**Expected behavior**:

- At any time, the 2 earliest deadline tasks are running
- Tasks migrate between cores depending on closest deadlines remaining
- Deadlines met

**Result**: PASS. Switch log shows tasks appearing on both C0 and C1 across different jobs, confirming migration, and no misses.

---

## Test 19 — Admission Control (U > 2.0 Rejected)

3 tasks each with U = 0.8. First two admitted (total U=1.6), third rejected since it would exceed 2.

**Params**:

- Task A: T=100, D=100, C=80 (U=0.8)
- Task B: T=100, D=100, C=80 (U=0.8)
- Task C: T=100, D=100, C=80 (U=0.8)

**Expected behavior**:

- Only A and B run, C gets rejected

**Result**: PASS. Prints matches expected ADMITTED/REJECTED pattern.

---

## Test 20 — Dynamic Task Creation

A spawner task creates 4 new EDF tasks at runtime, one every 500ms. Verifies that admission control and scheduling work correctly after the scheduler has started.

**Params**:

- Spawner: FreeRTOS task with priority 3
- Each worker: T=200, D=200, C=20 (U=0.1)
- Workers created at t = ~500ms, ~1000ms, ~1500ms, ~2000ms

**Expected behavior**:

- All 4 workers admitted (total U = 0.4, well under 2.0)
- Each worker starts scheduling immediately after creation
- Admitted count reaches 4

**Result**: PASS, all 4 tasks admitted and meet deadlines. Admitted count = 4.

---

## Test 21 — 100 Tasks Simultaneously

100 tasks running on 2 cores, stress test for the scheduler's ready list and admission control.

**Params**:

- 100 tasks, each T=1000, D=1000, C=1
- Total U = 100 \* (1/1000) = 0.1

**Expected behavior**:

- All 100 admitted (U = 0.1 << 2.0)
- No misses

**Result**: PASS, all 100 tasks admitted with no misses

---

## Test 22 — Deadline Miss Detection/Handle

A task runs longer than its deadline to trigger miss detection mechanism. First 2 jobs run 120ms with D=100, then subsequent jobs run normally at 50ms.

**Params**:

- Task: T=200, D=100, C=50 (declared), actual first 2 jobs run 120ms

**Expected behavior**:

- Jobs 1-2: run 120ms, exceed D=100 at tick 100/300, miss logged
- Jobs 3+: run 50ms, complete within D=100, no misses
- Task continues running after miss (not killed)

**Result**: PASS. Miss log entries for first 2 jobs, then tasks recovers and meets deadlines from job 3 onward. Confirms the expected overrun mechanism: log and continue.

---

# Partitioned EDF

## Test 23 - 2 Tasks, 2 Cores

2 identical EDF tasks that should run simultaneously on 1 core. Verifies first-fit scheduling.

**Params**:

- Task A: T=200, D=200, C=80
- Task B: T=200, D=200, C=80
- Total U = 0.80 for 1 core

**Expected behavior**:

- Both tasks run on same core (core 0), other core idle

**Result**: PASS. Logs confirms both tasks on C0, C1 idle.

## Test 24 - 3 Tasks, 2 Cores

3 tasks on 2 cores. One core should have 2 tasks, the other core 1 task, since no migration allowed.

**Params**:

- Task A: T=100, D=100, C=60 (U=0.6)
- Task B: T=100, D=100, C=30 (U=0.3)
- Task C: T=100, D=100, C=20 (U=0.2)
- Total U = 1.1, so all tasks admitted, with Task A and B assigned to core 0, Task C on core 1

**Expected behavior**:

- Core 0 runs Task A and B, core 1 runs Task C
- Deadlines met

**Result**: PASS. Logs confirms A and B on C0, C on C1, all deadlines met.

## Test 25 - Admission Control (U > 1.0 Rejected)

3 tasks, 2 tasks with U=0.9 and 1 task with U=0.2. Only the two tasks with U=0.9 should be admitted (one on each core), the U=0.2 task should be rejected since it would exceed 1.0 on either core.

**Params**:

- Task A: T=100, D=100, C=90 (U=0.9)
- Task B: T=100, D=100, C=90 (U=0.9)
- Task C: T=100, D=100, C=20 (U=0.2)
- Total U=0.9 on each core if A and B admitted, but C would push either core to 1.1. So, only A and B admitted, C rejected.

**Expected behavior**:

- Task A admitted to core 0, Task B admitted to core 1, Task C rejected
- Admitted count = 2
- Deadlines met for A and B

**Result**: PASS. Logs confirms A on C0, B on C1, C rejected, and both A and B meet deadlines.

## Test 26 - No Migration Across Jobs

3 tasks on 2 cores. Each task prints its running core via the switch log. Verify that each task always appears on the same core across all its jobs.

**Params**:

- Task A: T=100, D=100, C=40 → core 0
- Task B: T=150, D=150, C=40 → core 0
- Task C: T=200, D=200, C=70 → core 1
- Total U = 0.55 on core 0, 0.35 on core 1

**Expected behavior**:

- Task A and B always run on core 0, Task C always runs on core 1, across all jobs

**Result**: PASS. Switch log confirms A and B always on C0, C always on C1, with no migration across jobs.

## Test 27 - Deadline Miss Detection/Handle

Same structure as Test 22 but under partitioned scheduling. Task runs longer than its deadline to trigger miss detection mechanism.

**Params**:

- Task: T=200, D=100, C=50 (declared), actual first 2 jobs run 120ms

**Expected behavior**:

- Jobs 1-2: run 120ms, exceed D=100 at tick 100/300, miss logged
- Jobs 3+: run 50ms, complete within D=100, no misses
- Task continues running after miss (not killed)

**Result**: PASS. Miss log entries for first 2 jobs, then tasks recovers and meets deadlines from job 3 onward. Confirms the expected overrun mechanism: log and continue, even under partitioned scheduling.

## Test 28 - Dynamic Task Creation

A spawner task creates 5 new EDF tasks at runtime, one every 500ms. The last job should be rejected. Verifies that admission control and scheduling work correctly after the scheduler has started, under partitioned scheduling.

**Params**:

- Spawner: FreeRTOS task with priority 3
- Task 1: T=200, D=200, C=120 (U=0.6) → core 0
- Task 2: T=200, D=200, C=100 (U=0.5) → core 1
- Task 3: T=200, D=200, C=60 (U=0.3) → core 0
- Task 4: T=200, D=200, C=60 (U=0.3) → core 1
- Task 5: T=200, D=200, C=60 (U=0.3) → Rejected
- Workers created at t = ~500ms, ~1000ms, ~1500ms, ~2000ms, ~2500ms

**Expected behavior**:

- Tasks 1-4 admitted and scheduled on respective cores (1&3 on core 0, 2&4 on core 1)
- Task 5 rejected since it would exceed 1.0 on either core
- Admitted count reaches 4
- Deadlines met for admitted tasks

**Result**: PASS, all 4 tasks admitted and meet deadlines, Task 5 rejected as expected. Admitted count = 4.

## Test 29 - 100 Tasks Simultaneously

100 tasks running on 2 cores, stress test for the scheduler's ready list and admission control, under partitioned scheduling. Tasks should be distributed across cores based on first-fit admission.

**Params**:

- 100 tasks, each T=1000, D=1000, C=12 (U=0.012)
- Total U = 1.2, so expect first 82 tasks admitted on core 0 (U=0.984), next 18 tasks admitted on core 1 (U=0.216)

**Expected behavior**:

- First 82 tasks admitted to core 0, next 18 tasks admitted to core 1, remaining tasks rejected
- No misses for admitted tasks

**Result**: PASS, 82 tasks admitted on C0, 18 tasks admitted on C1, no misses. Logs confirm expected distribution and behavior.
