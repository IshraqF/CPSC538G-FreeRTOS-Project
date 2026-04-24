# Test Cases for CBS

Each test is selected at compile time via `#define TEST_CASE N` at the top of `main.c`.

---

## Test 30 — CBS Lifecycle Test

Basic end-to-end test: create a CBS server, submit 2 short jobs at separate times, verify both run to completion.

**Params**:

- CBS server: Qs=20ms, Ts=50ms (Us=0.4)
- Submitter: fixed-priority, submits job 1 at t=50ms, job 2 at t=150ms

**Expected behavior**:

- Both jobs execute and increment `ulJobRuns`
- `ulJobRuns == 2` by the end
- Rule 1 fires on each submission (server is idle each time)

**Result**: PASS. Both jobs run, `ulJobRuns == 2` confirmed.

---

## Test 31 — CBS Rule 3 Test

Submit a single job that runs longer than the server's budget. Verifies Rule 3 fires mid-job, postponing the deadline and refilling the budget so the job can continue.

**Params**:

- CBS server: Qs=20ms, Ts=50ms
- Job runtime: 60ms (exceeds Qs=20ms by 3×)

**Expected behavior**:

- Job starts and exhausts budget at ~20ms
- Rule 3 fires: deadline postponed by Ts=50ms, budget refilled to Qs=20ms
- Rule 3 fires again at ~40ms
- Job completes at ~60ms
- `[CBS] RULE3` lines visible in debug log

**Result**: PASS. Rule 3 fires twice, job completes after deadline postponements.

---

## Test 32 — CBS Rule 2 Test

Submit 2 short jobs within the same server period while residual budget remains. Verifies Rule 2 applies on the second submission (deadline and budget unchanged).

**Params**:

- CBS server: Qs=20ms, Ts=100ms (Us=0.2)
- Job 1 submitted at t=50ms, runtime=5ms
- Job 2 submitted at t=70ms (20ms later, within same period), runtime=5ms

**Expected behavior**:

- Job 1 triggers Rule 1
- After job 1 completes, budget still has 15ms remaining
- Job 2 arrives at t=70ms; server was idle so Rule 1/2 is re-evaluated
- Rule 2 applies
- Both jobs complete, `ulJobRuns == 2`

**Result**: PASS. Rule 1 applies to job 1, Rule 2 applies to job 2. Both jobs complete successfully.

---

## Test 33 — CBS + Hard EDF Task Test

Run a CBS server alongside a hard EDF periodic task. Verifies that CBS bandwidth isolation prevents the soft CBS job from starving the hard task.

**Params**:

- EDF task tau1: T=50ms, D=50ms, C=10ms (U=0.2)
- CBS server: Qs=20ms, Ts=50ms (Us=0.4)
- CBS job runtime: 80ms
- Submitter: EDF task (T=200ms, D=5ms, C=1ms), submits 1 long CBS job at t=50ms

**Expected behavior**:

- tau1 runs every 50ms and completes within its deadline throughout
- CBS job runs in the slack left by tau1; Rule 3 fires repeatedly
- After 250ms, `ulTau1Jobs >= 5` (tau1 ran at least 5 times)

**Result**: PASS. tau1 completes >= 5 jobs with no deadline misses. CBS job is throttled by Rule 3 as expected. Note: submitter is an EDF task here, avoiding the fixed-priority submission delay bug.

---

## Test 34 — CBS Stress Test, Two Jobs at Irregular Intervals

Run a CBS server and a hard EDF task simultaneously. Submit 2 variable-length jobs at irregular arrival times to stress the Rule 1/2 decision and Rule 3 throttling.

**Params**:

- EDF task tau1: T=70ms, D=70ms, C=40ms (U=0.57)
- CBS server: Qs=30ms, Ts=80ms (Us=0.375)
- Job J40: 40ms runtime, submitted at t=30ms
- Job J30: 30ms runtime, submitted at t=167ms (137ms after J40)
- Submitter: fixed-priority task (priority 1)

**Expected behavior**:

- tau1 runs every 70ms throughout
- Rule 1 fires for J40; it exhausts budget and Rule 3 fires until J40 completes
- J30 arrives well after J40's period; Rule 1 fires (server idle, old deadline stale)
- All CBS jobs complete

**Result**: PASS. All jobs complete, tau1 keeps running. Note: submitter is fixed-priority so submission of J30 is delayed until tau1 is idle (see bugs_CBS.md).

---

## Test 35 — CBS Stress Test, Three Jobs at Irregular Intervals (EDF Submitter)

Same setup as Test 34 but with 3 jobs and the submitter promoted to an EDF task, removing the fixed-priority submission delay.

**Params**:

- EDF task tau1: T=70ms, D=70ms, C=40ms (U=0.57)
- CBS server: Qs=30ms, Ts=80ms (Us=0.375)
- Job J40: 40ms runtime, submitted at t=25ms
- Job J30: 30ms runtime, submitted at t=62ms (37ms after J40)
- Job J50: 50ms runtime, submitted at t=153ms (91ms after J30)
- Submitter: EDF task (T=500ms, D=5ms, C=1ms), scheduled alongside tau1

**Expected behavior**:

- Submitter runs as an EDF task so submissions are not delayed by tau1
- J40 triggers Rule 1; exhausts budget, Rule 3 fires once
- J30 arrives while J40 executes, queued without Rule 1/2
- J50 arrives while J30 executes, queued without Rule 1/2
- tau1 meets all deadlines
- All 3 CBS jobs complete

**Result**: PASS. All 3 jobs complete, tau1 unaffected, submissions not delayed.
