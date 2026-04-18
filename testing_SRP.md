# Test Cases for SRP

Each test is selected at compile time via `#define TEST_CASE N` at the top of `main.c`.

---

## Test 8 — Basic SRP Blocking

2 EDF tasks sharing one resource, should demonstrate that the higher-priority task (H) is blocked while lower-priority task (L) holds the shared resource, and runs immediately after L releases it.

**Params**:
- Task H: T=200, D=200, C=30 (locks R for 10ms in the middle)
- Task L: T=600, D=600, C=280 (locks R at ~80ms, holds for 150ms)
- R ceiling = min(200, 600) = 200

**Expected behavior**:
- H starts at t=0 (since earlier deadline)
- L starts after H, acquires R, system ceiling drops to 200
- When H's next job arrives at t=200, H is SRP blocked (preemption level 200 & ceiling 200)
- When L releases R, ceiling returns to portMAX_DELAY, H immediately preempts
- Deadlines all met

**Result**: PASS. H is blocked during L's critical section and runs immediately on release. Pattern repeats consistently across hyperperiods.

---

## Test 13 — Nested Resources

2 tasks, 2 resources, where L takes R2 then nests R1 inside R2. Verifies the ceiling stack properly pushes/pops multiple entries and the owner-check allows L to resume after H preempts.

**Params**:
- Task H: T=200, D=200, C=30 (uses R1)
- Task L: T=600, D=600, C=300 (uses R2, then R1 nested inside R2)
- R1 ceiling = 200 (used by H & L)
- R2 ceiling = 600 (used by L)

**Expected behavior**:
- Ceiling stack: push R2(600) > push R1(200) > pop R1 restores to 600 > pop R2 goes back to portMAX_DELAY
- H is SRP-blocked at t=200 (ceiling=200 from L holding R1, H's preemption=200)
- L continues because it owns the ceiling entry (owner-check)
- When L releases R1, ceiling becomes 600, so H immediately preempts
- H completes, then L resumes to finish

**Result**: PASS. L completes jobs every 600 ticks, H completes every 200 ticks, zero deadline misses. Pattern identical across all observed cycles. Printf ordering confirms instant preemption: H's `job START` appears at the same tick as L's SRP unlock, and L's `released R1` appears after H's entire job completes. H blocked for 92ms consistently (arrives ~10ms into L's 100ms R1 critical section), within theoretical bound.

---

## Test 14 — Deadlock

2 tasks acquire 2 resources in opposite order. Without SRP, this would deadlock. SRP prevents it by blocking A from starting if B holds resources.

**Params**:
- Task A: T=300, D=300, C=60 (takes R1 first, then R2 nested)
- Task B: T=900, D=900, C=500 (takes R2 first, then R1 nested)
- R1 ceiling = R2 ceiling = min(300, 900) = 300

**Expected behavior**:
- When B holds R2 & R1, system ceiling = 300. A's preemption level = 300, too low, so A blocked.
- A cannot hold R1 if B holds R2, so deadlock impossible
- When B releases resources, ceiling equals `portMAX_DELAY` and A preempts
- A blocks at most once per job, for ~10ms (remaining time in B's critical section)

**Result**: PASS. No deadlock after 4500+ ticks, A consistently blocks for 10ms & tasks meet all deadlines.

---

## Test 15 — Internal State

Lock/unlock sequences checking that the system ceiling value and ceiling stack depth match expected values at every step. Covers single, double, and triple-nested locking.

**Params**:
- 3 resources: R1 (ceiling=100), R2 (ceiling=300), R3 (ceiling=500)
- 1 verification task runs a sequence of take/give operations
- After each operation, call `xSRPGetCurrentCeiling()` & `uxSRPGetCeilingStackDepth()` to compare against expected values

**Expected sequence (check after each step)**:
1. Initial: ceiling=MAX, depth=0
2. Take R2(300): ceiling=300, depth=1
3. Take R1(100) nested: ceiling=100, depth=2
4. Give R1: ceiling=300, depth=1
5. Give R2: ceiling=MAX, depth=0
6. Take R1(100): ceiling=100, depth=1
7. Take R3(500) nested: ceiling=100, depth=2 (R1 still min)
8. Take R2(300) triple-nested: ceiling=100, depth=3 (R1 still min)
9. Give R2: ceiling=100, depth=2
10. Give R3: ceiling=100, depth=1
11. Give R1: ceiling=MAX, depth=0

**Result**: PASS all 11/11 checks. This also confirms adding owner field to `SRPCeilingEntry_t` struct change doesn't break ceiling tracking.

---
## Test 16 — Admission Control with Blocking Times

8 admission control scenarios verify that blocking times are correctly incorporated into LL bound and demand bound paths. Each scenario checks: (1) behavior result (task handle returned or NULL), and (2) internal admitted count via `uxEDFGetAdmittedCount()`

**LL bound path (implicit deadline)**:

| # | Task | T | C | B | Expected | Reason |
|---|------|---|---|---|----------|--------|
| 1 | T1 | 1000 | 300 | 100 | ADMIT | C+B=400≤1000, U=0.30≤1.0 |
| 2 | T2 | 500 | 150 | 50 | ADMIT | C+B=200≤500, sum U=0.60≤1.0 |
| 3 | TR1 | 300 | 200 | 150 | REJECT | C+B=350>D=300 |
| 4 | TR2 | 400 | 300 | 101 | REJECT | C+B=401>D=400 (boundary) |
| 5 | TR3 | 200 | 130 | 0 | REJECT | sum U=0.60+0.65=1.25>1.0 |
| 6 | T3 | 2000 | 100 | 50 | ADMIT | C+B=150≤2000, sum U=0.65≤1.0 |

**Demand bound path (constrained)**:

| # | Task | T | D | C | B | Expected | Reason |
|---|------|---|---|---|---|----------|--------|
| 7 | T4 | 1000 | 800 | 100 | 50 | ADMIT | h(800)=300≤800 |
| 8 | TR4 | 800 | 800 | 200 | 400 | REJECT | h(800)=850>800 |

**Result**: PASS 8/8 scenarios produce correct ADMIT/REJECT decisions with expected admitted counts.

---

## Test 11 — 100 Tasks, No Stack Sharing

Create 100 separate EDF tasks, each with its own 256-word stack. Used as EDF baseline to compare with memory saved when SRP stack sharing is added.

**Result**: PASS. All 100 tasks created and admitted. Provides memory baseline (compare to test 12).

---

## Test 12 — 100 Tasks, Yes Stack Sharing

Create 1 dispatcher EDF task via `xTaskCreateEDFSharedGroup()`, runs 100 jobs sequentially per period & share a single stack. Demonstrates tasks with identical preemption level never occupy stack space simultaneously

**Result**: PASS. All 100 jobs execute on a single stack. Save ~99% reduction in memory vs. Test 11.

---

## Test 6 — Three-task EDF Ordering (Regression)

Verifies the modified `taskSELECT_HIGHEST_PRIORITY_TASK()` macro correctly selects earliest deadline task when SRP resources aren't present.

**Params**: A(T=300, D=100, C=20), B(T=200, D=150, C=30), C(T=400, D=200, C=40)

**Result**: PASS. 13 switch entries per hyperperiod (1200ms), which matches theoretical EDF schedule across observed hyperperiods.

## Test 7 — Constrained-deadline Demand (Regression)

Verifies admission control and EDF scheduling with short period, constrained deadline tasks.

**Params**: A(T=10, D=5, C=3), B(T=15, D=8, C=4)

**Result**: PASS. 5 switch entries per hyperperiod (30ms), again matches theoretical EDF schedule: offsets +0(A), +3(B), +10(A), +15(B), +20(A).
