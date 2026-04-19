# Test Cases for MP

# Global EDF

Each test is selected at compile time via `#define TEST_CASE N` at the top of `main.c`.

---

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
- Total U = 100 * (1/1000) = 0.1

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

TODO
