# Test Cases for EDF

Dev chooses which test to run by setting `#define TEST_CASE X` at the top of `main.c`.

---

## Test 1 — Single Implicit-Deadline Task

1 periodic task, verifies basic EDF scheduling loop works.

**Params**:

- Task: T=200, D=200, C=100 (U=0.5)

**Expected behavior**:

- Task admitted
- Task runs 100ms, sleeps 100ms, repeats
- Switch log shows periodic activation
- No deadline misses

**Result**: PASS

---

## Test 2 — 2 Tasks Near LL Limit

2 implicit-deadline tasks with total utilization 0.925, verifies EDF correctly interleaves tasks at high load.

**Params**:

- Task A: T=100, D=100, C=45 (U=0.45)
- Task B: T=200, D=200, C=95 (U=0.475)
- Total U=0.925

**Expected behavior**:

- Both admitted
- A runs every 100ms, B runs every 200ms
- A preempts B when A's deadline is earlier

**Result**: PASS. Both tasks meet deadlines & EDF preempts B for A when expected.

---

## Test 3 — EDF + Non-EDF Coexistence

1 EDF and 1 fixed-priority task, verifies the 2 scheduling domains can coexist.

**Params**:

- EDF Task: T=100, D=100, C=30
- Background Task: priority 1, prints every 50ms

**Expected behavior**:

- EDF task always runs first when ready
- Background task runs during EDF idle periods (70ms of slack per period)
- Background prints visible in UART output

**Result**: PASS. Background task runs in slack time but EDF task always preempts it.

---

## Test 4 — Admission Control

Verifies that admission control rejects a task set with U exceeding 1.0.

**Params**:

- Task A: T=100, D=100, C=60 (U=0.60) — should admit
- Task B: T=200, D=200, C=100 (U=0.50) — totals 1.10, should reject

**Expected behavior**:

- Task A: `xTaskCreateEDF` returns pdPASS
- Task B: `xTaskCreateEDF` returns pdFAIL
- So only Task A runs

**Result**: PASS

---

## Test 5 — Deadline Miss Detection

Intentionally run task past its deadline a task to trigger deadline miss detection.

**Params**:

- Task: T=200, D=200, C=50 (declared), actual runtime=210ms

**Expected behavior**:

- Task runs 210ms & exceeding its 200ms deadline
- `prvEDFCheckDeadlineMiss()` detects the overrun
- Miss logged into ring buffer

**Result**: PASS, miss detected and logged

---

## Test 6 — 3 Task EDF Ordering

3 constrained-deadline tasks with different deadlines, verifies the scheduler picks them in correct deadline order.

**Params**:

- Task A: T=300, D=100, C=20
- Task B: T=200, D=150, C=30
- Task C: T=400, D=200, C=40

**Expected behavior**:

- All 3 start ready
- Execution order: A > B > C
- Switch log confirms ordering

**Result**: PASS. Switch log shows A, B, C order at t=0 and correct reordering in later hyperperiods.

---

## Test 7 — Constrained-Deadline Admission (Processor Demand)

Two tasks with D < T, so code uses demand-bound test instead of LL bound.

**Params**:

- Task A: T=10, D=5, C=3
- Task B: T=15, D=8, C=4

**Expected behavior**:

- Both admitted via processor demand criterion
- `vEDFPrintStats()` prints admitted task table

**Result**: PASS. Both admitted, demand bound confirms schedulability.

---

## Test 9 — 100 Implicit-Deadline Tasks (LL Bound)

Stress test: 100 tasks, each T=1000, D=1000, C=9 (U_i=0.009, so total U=0.9).

**Expected behavior**:

- All 100 admitted
- Running utilization printed after each admission

**Result**: PASS, all 100 tasks admitted

---

## Test 10 — 100 Constrained-Deadline Tasks (Processor Demand)

Same as Test 9 but with D=500 (D < T), so forces demand-bound path.

**Expected behavior**:

- Demand bound is stricter than LL
- At t=500: h(500) = n\*9, fails when n > ~55 (504 > 500)
- ~55 tasks admitted, rest rejected

**Result**: PASS? ~55 tasks admitted before demand bound starts rejecting. Demonstrates the difference between LL and demand-bound admission.

---

## Test 36 — Admission Control Rejection at Runtime

Verifies that admission control correctly rejects a task whose admission would push total utilization above 1.0, even when the scheduler is already running with tasks executing in the background.

Unlike Test 4 (which creates all tasks before `vTaskStartScheduler()`), here a spawner task adds tasks one at a time 500ms apart after the scheduler has started.

**Params**:

- DYN_1: T=1000, D=1000, C=300 (U=0.30) — running total 0.30, should admit
- DYN_2: T=1000, D=1000, C=300 (U=0.30) — running total 0.60, should admit
- DYN_3: T=1000, D=1000, C=300 (U=0.30) — running total 0.90, should admit
- DYN_4: T=1000, D=1000, C=300 (U=0.30) — would push total to 1.20 > 1.0, should reject

**Expected behavior**:

- DYN_1 through DYN_3: `xTaskCreateEDF` returns pdPASS
- DYN_4: `xTaskCreateEDF` returns pdFAIL
- Final admitted count = 3
- DYN_1–3 continue running without deadline misses after DYN_4 is rejected

**Result**: PASS. DYN_1, DYN_2, DYN_3 admitted successfully; DYN_4 rejected as expected; all admitted tasks run without deadline misses.
